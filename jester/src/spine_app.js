import { createSpineScene } from "./scene/spine.js";
import { createHandTracker } from "./hands/tracker.js";
import { createHandSmoother } from "./hands/smooth.js";
import { label as gestureLabel } from "./hands/gestures.js";
import { createLink } from "./net/link.js";

const BONES = [
  [0,1],[1,2],[2,3],[3,4],
  [0,5],[5,6],[6,7],[7,8],
  [5,9],[9,10],[10,11],[11,12],
  [9,13],[13,14],[14,15],[15,16],
  [13,17],[17,18],[17,19],[19,20],
  [0,17],
];
const TIPS = [4, 8, 12, 16, 20];

const $ = (id) => document.getElementById(id);
const randomRoom = () => Array.from({ length: 4 }, () => "ABCDEFGHJKLMNPQRSTUVWXYZ23456789"[Math.floor(Math.random() * 32)]).join("");

function createSpineHud() {
  const root = $("hud");
  root.innerHTML = `
    <div class="hud-top-left">
      <div class="hud-name">MOONDAY SPINE 3D</div>
      <div class="hud-row"><span id="hud-dot"></span><span id="hud-status">SYSTEM · STANDBY</span></div>
      <div id="hud-hands">HANDS · 0</div>
    </div>
    <div class="hud-top-right">
      POINT · rotate and choose focus<br/>PINCH · move slice plane<br/>FIST · patient records<br/>TWO HANDS · zoom model
      <br/>MOUSE · drag rotate · wheel zoom · click focus
    </div>
    <div id="hud-flash"><div id="hud-flash-text"></div></div>
  `;
  const dot = root.querySelector("#hud-dot");
  const status = root.querySelector("#hud-status");
  const hands = root.querySelector("#hud-hands");
  const flash = root.querySelector("#hud-flash");
  const flashText = root.querySelector("#hud-flash-text");
  const colors = { pairing: "#ffd166", online: "#59d8ff", "view only": "#9fffb9", error: "#ff5b5b" };

  return {
    status(text) {
      status.textContent = "SYSTEM · " + text.toUpperCase();
      const color = colors[text.toLowerCase()] || "#59d8ff";
      dot.style.background = color;
      dot.style.boxShadow = `0 0 10px ${color}`;
    },
    handCount(labels) {
      hands.textContent = `HANDS · ${labels.length}` + (labels.length ? " · " + labels.join(" ") : "");
    },
    flash(text) {
      flashText.textContent = text;
      flash.classList.remove("go");
      void flash.offsetWidth;
      flash.classList.add("go");
    },
    drawHands(canvas, trackedHands) {
      const ctx = canvas.getContext("2d");
      if (canvas.width !== innerWidth || canvas.height !== innerHeight) {
        canvas.width = innerWidth;
        canvas.height = innerHeight;
      }
      ctx.clearRect(0, 0, canvas.width, canvas.height);
      ctx.shadowColor = "rgba(89,216,255,0.9)";
      ctx.shadowBlur = 12;
      ctx.strokeStyle = "rgba(120,225,255,0.85)";
      ctx.lineWidth = 2;
      for (const { landmarks } of trackedHands) {
        const px = (p) => [p.x * canvas.width, p.y * canvas.height];
        for (const [a, b] of BONES) {
          ctx.beginPath();
          ctx.moveTo(...px(landmarks[a]));
          ctx.lineTo(...px(landmarks[b]));
          ctx.stroke();
        }
        ctx.fillStyle = "rgba(180,240,255,0.9)";
        for (const p of landmarks) {
          const [x, y] = px(p);
          ctx.beginPath();
          ctx.arc(x, y, 2.5, 0, Math.PI * 2);
          ctx.fill();
        }
        ctx.fillStyle = "#eafcff";
        for (const tip of TIPS) {
          const [x, y] = px(landmarks[tip]);
          ctx.beginPath();
          ctx.arc(x, y, 4.5, 0, Math.PI * 2);
          ctx.fill();
        }
      }
      ctx.shadowBlur = 0;
    },
  };
}

async function main() {
  const hud = createSpineHud();
  const smoother = createHandSmoother();
  const spine = createSpineScene($("scene"), $("problem-panel"), $("records-panel"));
  let hands = [];
  let localMode = false;
  let tracker = null;
  let previous = performance.now() / 1000;

  fetch("/spine-records")
    .then((response) => response.ok ? response.json() : Promise.reject(new Error("records " + response.status)))
    .then((payload) => {
      spine.setRecords(payload.records || [], payload.source || "repository spine_records.json");
      hud.flash(`${payload.count || 0} REPO RECORDS LOADED`);
    })
    .catch((err) => {
      console.error(err);
      hud.flash("RECORD LOAD FAILED");
    });

  function goOnline(mode) {
    $("pair").style.display = "none";
    hud.status("online");
    hud.flash(mode === "phone" ? "PHONE HAND CONTROL LINKED" : "LOCAL HAND CONTROL ACTIVE");
  }

  const params = new URLSearchParams(location.search);
  let room = params.get("room");
  if (!room) {
    try { room = localStorage.getItem("moondayRoom"); } catch {}
  }
  if (!room) {
    room = randomRoom();
    try { localStorage.setItem("moondayRoom", room); } catch {}
  }

  const phoneBase = params.get("pub") || location.origin;
  const phoneURL = `${phoneBase}/phone.html?room=${room}`;
  $("pair-code").textContent = room;
  $("pair-url").textContent = phoneURL;
  $("qr").src = `https://api.qrserver.com/v1/create-qr-code/?size=220x220&margin=0&data=${encodeURIComponent(phoneURL)}`;
  $("boot").remove();
  const directMode = params.get("direct") === "1" || Boolean(window.jester);
  const cameraButton = $("camera-btn");
  $("pair").style.display = directMode ? "none" : "flex";
  hud.status(directMode ? "view only" : "pairing");
  if (directMode) hud.flash("MOONDAY SPINE READY");

  createLink({
    role: "display",
    room,
    onMessage: (msg) => {
      if (msg.type === "hands") hands = msg.hands || [];
      else if (msg.type === "peer" && msg.role === "phone") {
        if (msg.state === "joined") goOnline("phone");
        else if (msg.state === "left") {
          hands = [];
          if (!directMode) {
            $("pair").style.display = "flex";
            hud.status("pairing");
          }
        }
      }
    },
  });

  $("skip-pair").addEventListener("click", () => {
    $("pair").style.display = "none";
    hud.status("view only");
    hud.flash("SPINE MODEL READY");
  });

  async function enableLocalCamera() {
    try {
      const video = $("video");
      const stream = await navigator.mediaDevices.getUserMedia({ video: { facingMode: "user" }, audio: false });
      video.srcObject = stream;
      await video.play();
      $("cam").style.display = "block";
      tracker = await createHandTracker();
      localMode = true;
      cameraButton.classList.add("active");
      cameraButton.textContent = "CAMERA HANDS ACTIVE";
      goOnline("local");
    } catch (err) {
      console.error(err);
      cameraButton.classList.remove("active");
      cameraButton.textContent = "ENABLE CAMERA HANDS";
      alert("Camera failed: " + err.message);
    }
  }

  $("use-local").addEventListener("click", enableLocalCamera);
  cameraButton.addEventListener("click", enableLocalCamera);

  function frame(nowMs) {
    const now = nowMs / 1000;
    const dt = Math.min(now - previous, 0.05);
    previous = now;
    if (localMode && tracker) hands = tracker.detect($("video"), nowMs);
    const smoothed = smoother.smooth(hands, now);
    spine.updateHands(smoothed, now, dt);
    hud.drawHands($("overlay"), smoothed);
    hud.handCount(smoothed.map((hand) => gestureLabel(hand.landmarks)));
    spine.render(now);
    requestAnimationFrame(frame);
  }
  requestAnimationFrame(frame);
}

let started = false;
async function startOnce() {
  if (started) return;
  started = true;
  try {
    await main();
  } catch (err) {
    console.error(err);
    alert("Startup failed: " + err.message);
  }
}

$("start").addEventListener("click", startOnce);
if (window.jester || new URLSearchParams(location.search).get("direct") === "1") startOnce();
