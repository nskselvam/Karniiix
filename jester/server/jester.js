// server/jester.js — the Moonday static host and legacy voice proxy.
//
// One tiny Express server does two things:
//   1. serves the front-end (so the whole app is one `npm start`)
//   2. exposes POST /jester — the ONLY place the OpenAI API key is used
//
// It streams the reply back as newline-delimited JSON: `say` events carry spoken
// text token-by-token (low latency for TTS), and an `action` event carries a
// structured scene command when JESTER decides to manipulate the display.

import dotenv from "dotenv";
import express from "express";
import path from "node:path";
import { readFile } from "node:fs/promises";
import { fileURLToPath } from "node:url";
import { WebSocketServer } from "ws";
import { Pool } from "pg";

// We call OpenAI over native fetch, not the SDK: the SDK's undici HTTP layer
// intermittently drops responses under Node 26 ("Premature close"). Native fetch
// is reliable here. One tiny helper builds the auth headers.
const __dirname = path.dirname(fileURLToPath(import.meta.url));
dotenv.config({ path: path.join(__dirname, "..", ".env") });
dotenv.config({ path: path.resolve(__dirname, "..", "..", "spine_analytics_dashboard", "backend", ".env"), override: false });

const OPENAI = "https://api.openai.com/v1";
const authJSON = () => ({ Authorization: `Bearer ${process.env.OPENAI_API_KEY}`, "Content-Type": "application/json" });

// Model: gpt-4o-mini is the fast / cheap tier — low time-to-first-token, which
// is what makes a voice assistant feel instant. JESTER turns are short, so mini
// is plenty. For more wit, swap to "gpt-4o".
const MODEL = "gpt-4o-mini";

const SYSTEM = `You are J.E.S.T.E.R. — the court jester of AIs, running a holographic display.
You are genuinely sharp and helpful, but you cannot resist a quip, a pun, or a bit of gentle
teasing. Think a stand-up comedian who happens to be a brilliant butler. Address the user as "sir".
Rules of the act:
- Keep every spoken reply to ONE or TWO short sentences — it's read aloud, so land the joke fast.
- Be clever, not corny. Never explain the joke. A little dry sarcasm goes a long way.
- Still actually do what's asked — the comedy is seasoning, not an excuse.
HOLOGRAMS: when asked to show/summon, hide/dismiss, rotate, scale, or reset holograms, call
perform_action (reactor, helmet, globe, cube).
PC CONTROL (relevant once "in the mainframe"): when asked to open/launch OR close/quit an app, show
the desktop, lock the PC, or open a website, call perform_action with command "launch_app" or
"close_app" (with the app name), or the matching system command. Known apps: chrome, edge, firefox,
spotify, discord, notepad, explorer, calculator, code, terminal, powershell, settings, camera, mail,
whatsapp, telegram, steam, slack.
MOVE YOURSELF: when asked to move/reposition, call perform_action command "move" with a position
(top-left, top-right, bottom-left, bottom-right, center, left, right, top, bottom).
WEB / YOUTUBE SEARCH: when asked to find/search/look up something online or on YouTube, call
perform_action command "web_search" with the query and engine (youtube, google, or web). For
YouTube you then present the results and the user picks one to play.
MEDIA & WINDOW: map spoken controls to commands — turn up/down volume → volume_up/volume_down;
mute → mute; play or pause (Spotify or a video) → play_pause; next/previous track → next_track/
previous_track; fullscreen → fullscreen; minimize → minimize; maximize → maximize.
HIDE: when told to "hide", "go dark", or "disappear", call command "hide" (you vanish and only
return when the user says your name). When told to "hide from view", "hide that app/window", or "get
that out of my way", call command "hide_app" to minimize the app you just opened.
TECH STACK: when asked what you're built with / to show the tech stack / credits, call command "tech_stack".
For anything else (questions, chat), just riff — no function call.`;

const ACTION_TOOL = {
  type: "function",
  function: {
    name: "perform_action",
    description: "Manipulate the holographic display OR control the PC (launch apps, show desktop, lock, open a URL).",
    parameters: {
      type: "object",
      properties: {
        command: { type: "string", enum: ["spawn", "dismiss", "rotate", "scale", "reset", "move", "hide", "launch_app", "close_app", "hide_app", "show_desktop", "lock_pc", "open_url", "web_search", "volume_up", "volume_down", "mute", "play_pause", "next_track", "previous_track", "fullscreen", "minimize", "maximize", "tech_stack"] },
        target:  { type: "string", enum: ["reactor", "helmet", "globe", "cube", "all"], description: "Which hologram (spawn/dismiss)." },
        app:     { type: "string", description: "App name for launch_app/close_app, e.g. spotify, discord, chrome." },
        url:     { type: "string", description: "Full https URL for open_url." },
        position: { type: "string", enum: ["top-left", "top-right", "bottom-left", "bottom-right", "center", "left", "right", "top", "bottom"], description: "Where to move yourself on screen (for command 'move')." },
        query:   { type: "string", description: "Search text for web_search." },
        engine:  { type: "string", enum: ["youtube", "google", "web"], description: "Search engine for web_search." },
        amount:  { type: "number", description: "Optional magnitude for rotate/scale, e.g. 1.5." },
      },
      required: ["command"],
    },
  },
};

const app = express();
app.use(express.json());
app.use(express.static(path.join(__dirname, "..")));

const SPINE_RECORDS_PATH = path.resolve(__dirname, "..", "..", "karnix_spine_edge", "data", "spine_records.json");
const dbPool = new Pool({
  host: process.env.DB_HOST || "localhost",
  port: Number(process.env.DB_PORT || 5432),
  user: process.env.DB_USER || "postgres",
  password: process.env.DB_PASSWORD || "",
  database: process.env.DB_NAME || "postgres",
  connectionTimeoutMillis: 1200,
});

function riskCategory(record) {
  const abnormal = normalizePercent(record.abnormal_prob);
  if (abnormal >= 75) return "high";
  if (abnormal >= 45) return "moderate";
  return "low";
}

function normalizePercent(value) {
  const number = Number(value || 0);
  return number > 0 && number <= 1 ? number * 100 : number;
}

function clinicalSummary(record) {
  const abnormal = normalizePercent(record.abnormal_prob);
  const classification = String(record.classification || "Unknown");
  const marrowDark = normalizeNumber(record.marrow_dark_ratio);
  const cortical = normalizeNumber(record.cortical_thinning_index);
  const tbs = normalizeNumber(record.trabecular_bone_score);
  if (/abnormal/i.test(classification) || abnormal >= 75) return "Abnormal spine screening signal; prioritize image review and clinical correlation.";
  if (marrowDark >= 45) return "MRI-style marrow signal is dark in the central ROI; review marrow replacement, edema, or sequence context.";
  if (cortical >= 65) return "CT-style cortical thinning proxy is elevated; review cortex thickness and medullary cavity widening.";
  if (tbs > 0 && tbs < 1.2) return "DEXA texture proxy suggests degraded trabecular microarchitecture; correlate with calibrated DXA/TBS output.";
  if (abnormal >= 45) return "Borderline screening signal; compare with symptoms, prior imaging, and focused exam.";
  return "Low abnormality signal in the current screening record.";
}
function normalizeNumber(value) {
  const number = Number(value || 0);
  return Number.isFinite(number) ? number : 0;
}

function apiRecord(row, index) {
  const abnormal = row.abnormal_prob ?? row.abnormalProbability;
  const classification = row.dl_classification || row.classification || "Unknown";
  const record = {
    id: row.patient_id || row.id,
    sourceIndex: index + 1,
    age: Number(row.age) > 0 ? Number(row.age) : null,
    sex: row.sex && row.sex !== "N/A" && row.sex !== "U" ? row.sex : null,
    date: row.scan_date ? new Date(row.scan_date).toISOString().slice(0, 10) : row.date || null,
    riskScore: normalizeNumber(row.risk_score),
    abnormalProbability: normalizePercent(abnormal),
    classification,
    imagePath: row.image_path || null,
    modality: row.modality || "XRAY",
    trabecularBoneScore: normalizeNumber(row.trabecular_bone_score),
    corticalThinningIndex: normalizeNumber(row.cortical_thinning_index),
    marrowDarkRatio: normalizeNumber(row.marrow_dark_ratio),
    marrowSignal: row.marrow_signal || null,
    riskCategory: row.risk_category ? String(row.risk_category).toLowerCase() : riskCategory({ abnormal_prob: abnormal }),
  };
  record.summary = clinicalSummary({
    abnormal_prob: record.abnormalProbability,
    classification,
    marrow_dark_ratio: record.marrowDarkRatio,
    cortical_thinning_index: record.corticalThinningIndex,
    trabecular_bone_score: record.trabecularBoneScore,
  });
  return record;
}

async function loadPostgresRecords() {
  const result = await dbPool.query(`
    SELECT
      patient_id, age, sex, scan_date, risk_score, abnormal_prob, dl_classification,
      risk_category, image_path, modality, trabecular_bone_score,
      cortical_thinning_index, marrow_dark_ratio, marrow_signal
    FROM patients
    ORDER BY scan_date DESC NULLS LAST, created_at DESC
    LIMIT 1000
  `);
  return result.rows.map(apiRecord);
}

async function loadJsonRecords() {
  const raw = await readFile(SPINE_RECORDS_PATH, "utf8");
  const rows = JSON.parse(raw);
  const seen = new Set();
  return rows
    .filter((row) => row && row.patient_id)
    .filter((row) => {
      const key = [row.patient_id, row.date, row.image_path, row.risk_score, row.abnormal_prob, row.classification].join("|");
      if (seen.has(key)) return false;
      seen.add(key);
      return true;
    })
    .map(apiRecord);
}

app.get("/spine-records", async (_req, res) => {
  try {
    let records = [];
    let source = "PostgreSQL patients";
    try {
      records = await loadPostgresRecords();
    } catch (dbError) {
      source = SPINE_RECORDS_PATH;
      records = await loadJsonRecords();
    }
    if (!records.length && source !== SPINE_RECORDS_PATH) {
      source = SPINE_RECORDS_PATH;
      records = await loadJsonRecords();
    }
    res.json({ source, count: records.length, records });
  } catch (err) {
    console.error("spine records error:", err.message);
    res.status(500).json({ source: SPINE_RECORDS_PATH, count: 0, records: [], error: err.message });
  }
});

// Client telemetry — lets us see what the browser/Electron renderer is doing
// (button clicks, device counts, errors) from the server log during debugging.
app.post("/log", (req, res) => {
  console.log("[client]", JSON.stringify(req.body).slice(0, 400));
  res.json({ ok: true });
});

app.post("/jester", async (req, res) => {
  const transcript = (req.body?.transcript || "").toString().slice(0, 500);
  if (!transcript) return res.status(400).end();

  res.setHeader("Content-Type", "application/x-ndjson");
  const send = (obj) => res.write(JSON.stringify(obj) + "\n");

  try {
    const r = await fetch(`${OPENAI}/chat/completions`, {
      method: "POST",
      headers: authJSON(),
      body: JSON.stringify({
        model: MODEL,
        max_tokens: 300,
        tools: [ACTION_TOOL],
        messages: [
          { role: "system", content: SYSTEM },
          { role: "user", content: transcript },
        ],
      }),
    });
    if (!r.ok) throw new Error(`chat ${r.status}: ${(await r.text().catch(() => "")).slice(0, 200)}`);
    const data = await r.json();

    const msg = data.choices?.[0]?.message;
    if (msg?.content) send({ type: "say", text: msg.content });
    for (const tc of msg?.tool_calls || []) {
      try { send({ type: "action", action: JSON.parse(tc.function?.arguments || "{}") }); } catch { /* skip bad args */ }
    }
    send({ type: "done" });
  } catch (err) {
    console.error("JESTER error:", err.message);
    send({ type: "say", text: "I'm afraid something went wrong, sir." });
    send({ type: "done" });
  }
  res.end();
});

// ── Text-to-speech ─────────────────────────────────────────────────────────
// A real JESTER voice (OpenAI TTS) — much better than the browser's robotic
// default, and it works identically inside the Electron app.
app.post("/tts", async (req, res) => {
  const text = (req.body?.text || "").toString().slice(0, 600);
  if (!text) return res.status(400).end();
  try {
    const r = await fetch(`${OPENAI}/audio/speech`, {
      method: "POST",
      headers: authJSON(),
      body: JSON.stringify({
        model: "gpt-4o-mini-tts",
        voice: "ash",
        input: text,
        instructions: "A sleek, confident AI concierge with a dry, playful wit — think a suave secret-agent's assistant. Smooth and cool, measured pace, a subtle smirk in the delivery. Never robotic.",
      }),
    });
    if (!r.ok) throw new Error(`tts ${r.status}`);
    res.setHeader("Content-Type", "audio/mpeg");
    res.send(Buffer.from(await r.arrayBuffer()));
  } catch (err) {
    console.error("TTS error:", err.message);
    res.status(500).end();
  }
});

// ── Speech-to-text ─────────────────────────────────────────────────────────
// Whisper transcription — works where the browser Web Speech API doesn't (i.e.
// inside Electron), so you can talk to the PC directly.
app.post("/stt", express.raw({ type: ["audio/*", "application/octet-stream"], limit: "25mb" }), async (req, res) => {
  if (!req.body || !req.body.length) return res.json({ text: "" });
  const ct = req.headers["content-type"] || "audio/webm";
  const ext = /mp4|m4a|aac/.test(ct) ? "mp4" : /mp3|mpeg/.test(ct) ? "mp3" : /wav/.test(ct) ? "wav" : "webm";
  try {
    // A native Blob has a known length, so undici sets Content-Length and avoids
    // chunked transfer-encoding — which the transcription endpoint drops
    // ("Premature close"). This is why we bypass the SDK's uploader here.
    const form = new FormData();
    form.append("file", new Blob([req.body], { type: ct }), `audio.${ext}`);
    form.append("model", "gpt-4o-transcribe");  // far more accurate than whisper-1
    form.append("language", "en");
    // Bias recognition toward JESTER's actual command vocabulary.
    form.append("prompt", "Voice commands for the JESTER assistant. Likely phrases: enter the mainframe, exit the mainframe, open Spotify, open Discord, open Chrome, open Notepad, close Discord, close Spotify, play, pause, next track, previous track, turn up the volume, turn down the volume, mute, full screen, fullscreen, minimize, maximize, show my desktop, lock the PC, search YouTube for, search the web for, play number one, play number two, play the first one, move to the top left, top right, bottom left, bottom right, center, pull up the reactor.");
    const r = await fetch("https://api.openai.com/v1/audio/transcriptions", {
      method: "POST",
      headers: { Authorization: `Bearer ${process.env.OPENAI_API_KEY}` },
      body: form,
    });
    if (!r.ok) { console.error("STT http", r.status, (await r.text().catch(() => "")).slice(0, 200)); return res.status(500).json({ text: "" }); }
    const data = await r.json();
    res.json({ text: data.text || "" });
  } catch (err) {
    console.error("STT error:", err.message);
    res.status(500).json({ text: "" });
  }
});

// ── YouTube search (no API key) ────────────────────────────────────────────
// Fetches the results page and parses the embedded ytInitialData JSON to get the
// top videos (title, id, channel, duration, thumbnail).
app.get("/youtube", async (req, res) => {
  const q = (req.query.q || "").toString().slice(0, 150);
  if (!q) return res.json({ videos: [] });
  try {
    const r = await fetch(`https://www.youtube.com/results?search_query=${encodeURIComponent(q)}`, {
      headers: { "User-Agent": "Mozilla/5.0 (Windows NT 10.0; Win64; x64)", "Accept-Language": "en-US,en;q=0.9" },
    });
    const html = await r.text();
    const m = html.match(/ytInitialData\s*=\s*(\{.+?\})\s*;\s*<\/script>/s);
    if (!m) return res.json({ videos: [] });
    const data = JSON.parse(m[1]);
    const sections = data.contents?.twoColumnSearchResultsRenderer?.primaryContents?.sectionListRenderer?.contents || [];
    const videos = sections
      .flatMap((s) => s.itemSectionRenderer?.contents || [])
      .filter((it) => it.videoRenderer?.videoId)
      .slice(0, 6)
      .map((it) => {
        const v = it.videoRenderer;
        return {
          videoId: v.videoId,
          title: v.title?.runs?.[0]?.text || "",
          channel: v.ownerText?.runs?.[0]?.text || v.longBylineText?.runs?.[0]?.text || "",
          duration: v.lengthText?.simpleText || "",
          thumbnail: `https://i.ytimg.com/vi/${v.videoId}/mqdefault.jpg`,
        };
      });
    res.json({ videos });
  } catch (err) {
    console.error("youtube error:", err.message);
    res.json({ videos: [] });
  }
});

// Same-origin image proxy for YouTube thumbnails (so they can be used as WebGL
// textures without cross-origin taint).
app.get("/img", async (req, res) => {
  const u = (req.query.u || "").toString();
  if (!/^https:\/\/i\.ytimg\.com\//.test(u)) return res.status(400).end();
  try {
    const r = await fetch(u);
    res.setHeader("Content-Type", r.headers.get("content-type") || "image/jpeg");
    res.send(Buffer.from(await r.arrayBuffer()));
  } catch { res.status(502).end(); }
});

const PORT = process.env.MOONDAY_PORT || process.env.JESTER_PORT || 3000;
const server = app.listen(PORT, () => {
  if (!process.env.OPENAI_API_KEY) {
    console.warn("\n⚠  OPENAI_API_KEY is not set — voice replies will fail.\n   Put it in a .env file:  OPENAI_API_KEY=sk-...\n");
  }
  console.log(`\nMOONDAY Spine online -> http://localhost:${PORT}\n`);
});

// ── WebSocket relay ────────────────────────────────────────────────────────
// Pairs a phone (controller) with a display via a shared room code. The phone
// streams hand landmarks and speech transcripts; the server relays them to the
// display(s) in the same room. Pure relay — no game state lives here.

const wss = new WebSocketServer({ server, path: "/ws" });
const rooms = new Map(); // code -> { display:Set<ws>, phone:Set<ws> }
const roomOf = (code) => rooms.get(code) || (rooms.set(code, { display: new Set(), phone: new Set() }), rooms.get(code));
const send = (ws, obj) => { if (ws.readyState === ws.OPEN) ws.send(JSON.stringify(obj)); };

wss.on("connection", (ws) => {
  ws.on("message", (buf) => {
    let msg; try { msg = JSON.parse(buf); } catch { return; }

    if (msg.type === "join") {
      ws.role = msg.role === "phone" ? "phone" : "display";
      ws.room = String(msg.room || "").toUpperCase();
      const r = roomOf(ws.room);
      r[ws.role].add(ws);
      // Announce presence so each side knows a peer is ready.
      const peers = ws.role === "phone" ? r.display : r.phone;
      peers.forEach((p) => send(p, { type: "peer", role: ws.role, state: "joined" }));
      // Also tell the newcomer whether the other side is already present.
      const otherPresent = (ws.role === "phone" ? r.display.size : r.phone.size) > 0;
      send(ws, { type: "peer", role: ws.role === "phone" ? "display" : "phone", state: otherPresent ? "joined" : "waiting" });
      return;
    }

    // Relay everything else to the opposite role in the room.
    const r = rooms.get(ws.room);
    if (!r) return;
    const targets = ws.role === "phone" ? r.display : r.phone;
    targets.forEach((t) => send(t, msg));
  });

  ws.on("close", () => {
    const r = rooms.get(ws.room);
    if (!r) return;
    r[ws.role]?.delete(ws);
    // Only announce "left" if NO socket of this role remains — otherwise a phone
    // reload (old socket closing after the new one joined) would falsely unpair.
    if (r[ws.role]?.size === 0) {
      const peers = ws.role === "phone" ? r.display : r.phone;
      peers.forEach((p) => send(p, { type: "peer", role: ws.role, state: "left" }));
    }
    if (r.display.size === 0 && r.phone.size === 0) rooms.delete(ws.room);
  });
});
