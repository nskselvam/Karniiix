import * as THREE from "three";
import { EffectComposer } from "three/addons/postprocessing/EffectComposer.js";
import { RenderPass } from "three/addons/postprocessing/RenderPass.js";
import { UnrealBloomPass } from "three/addons/postprocessing/UnrealBloomPass.js";
import { OutputPass } from "three/addons/postprocessing/OutputPass.js";
import { isGrab, isPinch, isPoint, pinchStrength } from "../hands/gestures.js";

const VERTEBRAE = ["C1", "C2", "C3", "C4", "C5", "C6", "C7", "T1", "T2", "T3", "T4", "T5", "T6", "T7", "T8", "T9", "T10", "T11", "T12", "L1", "L2", "L3", "L4", "L5", "S1"];
const ANATOMY_COUNT = VERTEBRAE.length;

const CONDITIONS = [
  { name: "Scoliosis curve", region: "Thoracic to lumbar", severity: "High", metric: "Cobb 31 deg", problem: "Lateral curvature shifts load across the discs and facet joints.", action: "Track curve progression, posture loading, and orthopedic follow-up.", color: 0xff5b5b, vertebra: 5 },
  { name: "Compression fracture", region: "T12", severity: "High", metric: "Grade 2 collapse", problem: "Anterior body height loss can destabilize the sagittal profile.", action: "Check trauma history, osteoporosis markers, and serial imaging.", color: 0xff7a3d, vertebra: 9 },
  { name: "Disc degeneration", region: "L4-L5 / L5-S1", severity: "Moderate", metric: "Grade 3 disc change", problem: "Disc height loss narrows foraminal space and can irritate nerve roots.", action: "Correlate with pain pattern, mobility, and MRI findings.", color: 0xffc857, vertebra: 14 },
  { name: "Nerve impingement risk", region: "Right lumbar root", severity: "Moderate", metric: "Foraminal narrowing", problem: "Root path is crowded by disc and alignment changes.", action: "Review symptoms, reflexes, and dermatomal radiation.", color: 0x67f0ff, vertebra: 15 },
];

function roundedRect(ctx, x, y, w, h, r) {
  ctx.beginPath();
  ctx.moveTo(x + r, y);
  ctx.lineTo(x + w - r, y);
  ctx.quadraticCurveTo(x + w, y, x + w, y + r);
  ctx.lineTo(x + w, y + h - r);
  ctx.quadraticCurveTo(x + w, y + h, x + w - r, y + h);
  ctx.lineTo(x + r, y + h);
  ctx.quadraticCurveTo(x, y + h, x, y + h - r);
  ctx.lineTo(x, y + r);
  ctx.quadraticCurveTo(x, y, x + r, y);
}

function makeLabel(text, color) {
  const canvas = document.createElement("canvas");
  canvas.width = 512;
  canvas.height = 128;
  const ctx = canvas.getContext("2d");
  ctx.font = "700 38px Rajdhani, sans-serif";
  ctx.fillStyle = "rgba(3, 7, 10, 0.72)";
  ctx.strokeStyle = color;
  ctx.lineWidth = 2;
  roundedRect(ctx, 10, 18, 492, 80, 10);
  ctx.fill();
  ctx.stroke();
  ctx.fillStyle = color;
  ctx.textAlign = "center";
  ctx.textBaseline = "middle";
  ctx.fillText(text, 256, 59);
  const texture = new THREE.CanvasTexture(canvas);
  const sprite = new THREE.Sprite(new THREE.SpriteMaterial({ map: texture, transparent: true, depthWrite: false }));
  sprite.scale.set(1.9, 0.48, 1);
  return sprite;
}

function glow(color, opacity) {
  return new THREE.MeshBasicMaterial({ color, transparent: true, opacity, blending: THREE.AdditiveBlending, depthWrite: false });
}

function makeVertebra(index, name) {
  const group = new THREE.Group();
  const bodyMaterial = new THREE.MeshStandardMaterial({ color: 0xbfefff, roughness: 0.42, metalness: 0.15, transparent: true, opacity: 0.86, emissive: 0x06151b });
  const body = new THREE.Mesh(new THREE.BoxGeometry(1.05, 0.25, 0.58, 5, 1, 3), bodyMaterial);
  const arch = new THREE.Mesh(new THREE.TorusGeometry(0.37, 0.035, 8, 28, Math.PI * 1.35), new THREE.MeshStandardMaterial({ color: 0x84e7ff, roughness: 0.35, metalness: 0.25, transparent: true, opacity: 0.72 }));
  arch.rotation.x = Math.PI / 2;
  arch.position.z = -0.27;
  const spinous = new THREE.Mesh(new THREE.ConeGeometry(0.07, 0.5, 8), new THREE.MeshStandardMaterial({ color: 0x9eefff, roughness: 0.45, transparent: true, opacity: 0.72 }));
  spinous.rotation.x = Math.PI / 2;
  spinous.position.z = -0.55;
  const wire = new THREE.LineSegments(new THREE.WireframeGeometry(body.geometry), new THREE.LineBasicMaterial({ color: 0xe7fbff, transparent: true, opacity: 0.38 }));
  group.add(body, arch, spinous, wire);
  group.userData = { index, name, body };
  return group;
}

function makeNerve(side, highlight) {
  const curve = new THREE.CatmullRomCurve3([
    new THREE.Vector3(0.35 * side, 0.05, -0.08),
    new THREE.Vector3(0.9 * side, -0.05, -0.22),
    new THREE.Vector3(1.55 * side, -0.25, -0.4),
  ]);
  return new THREE.Mesh(new THREE.TubeGeometry(curve, 18, 0.018, 8, false), glow(highlight ? 0x67f0ff : 0x347c92, highlight ? 0.76 : 0.34));
}

function pct(value) {
  return `${Number(value || 0).toFixed(1)}%`;
}

function score(value, decimals = 1) {
  const number = Number(value || 0);
  return number > 0 ? number.toFixed(decimals) : "N/A";
}

function labelUnknown(value) {
  return value == null || value === "" ? "Unknown" : value;
}

function escapeHtml(value) {
  return String(value ?? "")
    .replace(/&/g, "&amp;")
    .replace(/</g, "&lt;")
    .replace(/>/g, "&gt;")
    .replace(/"/g, "&quot;")
    .replace(/'/g, "&#39;");
}

function recordTone(record) {
  if (record.riskCategory === "high") return "high";
  if (record.riskCategory === "moderate") return "moderate";
  return "low";
}

function recordMatches(record, query) {
  if (!query) return true;
  const text = [
    record.id,
    record.classification,
    record.date,
    record.imagePath,
    record.summary,
    record.riskCategory,
    record.modality,
    record.marrowSignal,
    record.trabecularBoneScore,
    record.corticalThinningIndex,
    record.marrowDarkRatio,
  ].join(" ").toLowerCase();
  return text.includes(query.toLowerCase());
}

function sortRecords(records, mode) {
  const sorted = [...records];
  if (mode === "risk") return sorted.sort((a, b) => Number(a.riskScore || 0) - Number(b.riskScore || 0));
  if (mode === "date") return sorted.sort((a, b) => String(b.date || "").localeCompare(String(a.date || "")));
  return sorted.sort((a, b) => Number(b.abnormalProbability || 0) - Number(a.abnormalProbability || 0));
}

export function createSpineScene(container, panel, recordsPanel) {
  const scene = new THREE.Scene();
  scene.fog = new THREE.FogExp2(0x03070a, 0.055);

  const camera = new THREE.PerspectiveCamera(42, 1, 0.1, 100);
  camera.position.set(0, 1.1, 9);
  camera.lookAt(0, 0, 0);

  const renderer = new THREE.WebGLRenderer({ antialias: true, alpha: true });
  renderer.setPixelRatio(Math.min(devicePixelRatio, 2));
  renderer.setClearColor(0x03070a, 1);
  renderer.toneMapping = THREE.ACESFilmicToneMapping;
  renderer.toneMappingExposure = 1;
  container.appendChild(renderer.domElement);

  const composer = new EffectComposer(renderer);
  composer.addPass(new RenderPass(scene, camera));
  const bloom = new UnrealBloomPass(new THREE.Vector2(1, 1), 0.75, 0.42, 0.2);
  composer.addPass(bloom);
  composer.addPass(new OutputPass());

  scene.add(new THREE.HemisphereLight(0x9eefff, 0x09121a, 1.35));
  const key = new THREE.DirectionalLight(0xffffff, 2.3);
  key.position.set(3, 4, 5);
  scene.add(key);

  const root = new THREE.Group();
  scene.add(root);

  const grid = new THREE.GridHelper(9, 18, 0x1ba6c9, 0x0d3444);
  grid.position.y = -3.05;
  grid.material.transparent = true;
  grid.material.opacity = 0.25;
  scene.add(grid);

  const vertebrae = [];
  const discs = [];
  const spacing = 0.38;
  const top = (VERTEBRAE.length - 1) * spacing * 0.5;
  for (let i = 0; i < VERTEBRAE.length; i += 1) {
    const y = top - i * spacing;
    const curveX = Math.sin((i / (VERTEBRAE.length - 1)) * Math.PI * 1.15) * 0.42 - 0.1;
    const vertebra = makeVertebra(i, VERTEBRAE[i]);
    vertebra.position.set(curveX, y, 0);
    vertebra.rotation.z = curveX * -0.18;
    vertebra.rotation.y = Math.sin(i * 0.56) * 0.12;
    vertebrae.push(vertebra);
    root.add(vertebra);

    if (i < VERTEBRAE.length - 1) {
      const disc = new THREE.Mesh(new THREE.CylinderGeometry(0.48, 0.5, 0.08, 36), new THREE.MeshStandardMaterial({ color: 0x2ee6a6, emissive: 0x0e6a52, roughness: 0.38, transparent: true, opacity: 0.58 }));
      disc.rotation.x = Math.PI / 2;
      disc.position.set(curveX * 0.98, y - spacing * 0.5, 0.01);
      discs.push(disc);
      root.add(disc);
    }
  }

  root.add(new THREE.Mesh(new THREE.CylinderGeometry(0.17, 0.17, VERTEBRAE.length * spacing + 0.5, 32), glow(0x59d8ff, 0.18)));

  const skullBase = new THREE.Mesh(new THREE.SphereGeometry(0.42, 28, 16), new THREE.MeshStandardMaterial({ color: 0xf3f0e7, roughness: 0.62, metalness: 0.02, transparent: true, opacity: 0.9 }));
  skullBase.scale.set(1.15, 0.72, 0.92);
  skullBase.position.set(vertebrae[0].position.x - 0.05, vertebrae[0].position.y + 0.62, 0.02);
  root.add(skullBase);

  const pelvis = new THREE.Group();
  const pelvisMaterial = new THREE.MeshStandardMaterial({ color: 0xf0eee5, roughness: 0.66, metalness: 0.02, transparent: true, opacity: 0.88 });
  for (const side of [-1, 1]) {
    const ilium = new THREE.Mesh(new THREE.SphereGeometry(0.58, 28, 16), pelvisMaterial);
    ilium.scale.set(0.72, 1.05, 0.38);
    ilium.rotation.z = side * 0.28;
    ilium.position.set(side * 0.72, -0.2, 0.03);
    pelvis.add(ilium);
    const hip = new THREE.Mesh(new THREE.SphereGeometry(0.12, 18, 10), glow(0xffd166, 0.62));
    hip.position.set(side * 0.92, -0.54, -0.04);
    pelvis.add(hip);
  }
  pelvis.position.set(vertebrae[VERTEBRAE.length - 1].position.x, vertebrae[VERTEBRAE.length - 1].position.y - 0.72, 0);
  root.add(pelvis);

  for (const side of [-1, 1]) {
    for (let i = 11; i < 17; i += 1) {
      const nerve = makeNerve(side, i === 15 && side === 1);
      nerve.position.copy(vertebrae[i].position);
      root.add(nerve);
    }
  }

  const markers = CONDITIONS.map((condition, i) => {
    const marker = new THREE.Group();
    const ring = new THREE.Mesh(new THREE.TorusGeometry(0.75, 0.025, 8, 56), glow(condition.color, 0.82));
    ring.rotation.x = Math.PI / 2;
    const pulse = new THREE.Mesh(new THREE.SphereGeometry(0.12, 16, 10), glow(condition.color, 0.95));
    const label = makeLabel(condition.name, `#${condition.color.toString(16).padStart(6, "0")}`);
    label.position.set(i % 2 ? -1.65 : 1.65, 0.08, 0.2);
    marker.add(ring, pulse, label);
    marker.position.copy(vertebrae[condition.vertebra].position);
    marker.position.z += 0.04;
    marker.userData = { condition, ring, pulse, label };
    root.add(marker);
    return marker;
  });

  const slicePlane = new THREE.Mesh(new THREE.BoxGeometry(4.2, 0.035, 2.2), glow(0xffffff, 0.22));
  root.add(slicePlane);

  const state = { rotateX: -0.08, rotateY: 0.28, targetX: -0.08, targetY: 0.28, zoom: 1, targetZoom: 1, slice: 0.72, targetSlice: 0.72, selected: 0, lastSelect: 0, records: [], recordSource: "", recordsVisible: false, recordFilter: "all", recordSort: "abnormal", recordQuery: "", mouseDown: false, mouseMoved: false, mouseX: 0, mouseY: 0 };

  function resize() {
    const width = innerWidth;
    const height = innerHeight;
    renderer.setSize(width, height);
    composer.setSize(width, height);
    bloom.setSize(width, height);
    camera.aspect = width / height;
    camera.updateProjectionMatrix();
  }
  addEventListener("resize", resize);
  resize();

  const canvas = renderer.domElement;
  canvas.style.cursor = "grab";
  canvas.addEventListener("pointerdown", (event) => {
    if (event.button !== 0) return;
    state.mouseDown = true;
    state.mouseMoved = false;
    state.mouseX = event.clientX;
    state.mouseY = event.clientY;
    canvas.setPointerCapture?.(event.pointerId);
    canvas.style.cursor = "grabbing";
  });
  canvas.addEventListener("pointermove", (event) => {
    if (!state.mouseDown) return;
    const dx = event.clientX - state.mouseX;
    const dy = event.clientY - state.mouseY;
    if (Math.abs(dx) + Math.abs(dy) > 2) state.mouseMoved = true;
    state.targetY += dx * 0.012;
    state.targetX = THREE.MathUtils.clamp(state.targetX + dy * 0.008, -1.2, 1.2);
    state.mouseX = event.clientX;
    state.mouseY = event.clientY;
  });
  canvas.addEventListener("pointerup", (event) => {
    if (event.button !== 0) return;
    if (!state.mouseMoved) {
      const fraction = THREE.MathUtils.clamp(event.offsetX / Math.max(1, canvas.clientWidth), 0, 0.9999);
      state.selected = Math.floor(fraction * CONDITIONS.length);
      updatePanel();
    }
    state.mouseDown = false;
    canvas.releasePointerCapture?.(event.pointerId);
    canvas.style.cursor = "grab";
  });
  canvas.addEventListener("pointercancel", () => {
    state.mouseDown = false;
    canvas.style.cursor = "grab";
  });
  canvas.addEventListener("wheel", (event) => {
    event.preventDefault();
    state.targetZoom = THREE.MathUtils.clamp(state.targetZoom - event.deltaY * 0.001, 0.72, 1.8);
  }, { passive: false });

  function updatePanel() {
    const condition = CONDITIONS[state.selected];
    panel.innerHTML = `
      <div class="spine-kicker">KARNIX SPINE ANALYSIS</div>
      <h2>${condition.name}</h2>
      <div class="spine-meta"><span>${condition.region}</span><span>${condition.severity}</span><span>${condition.metric}</span></div>
      <p>${condition.problem}</p>
      <p>${condition.action}</p>
      <div class="spine-stats">
        <div><b>${ANATOMY_COUNT}</b><span>vertebrae shown</span></div>
        <div><b>${Math.round(state.slice * 100)}%</b><span>slice level</span></div>
        <div><b>${condition.severity}</b><span>focus risk</span></div>
      </div>
    `;
  }
  updatePanel();

  function renderRecordsPanel() {
    if (!recordsPanel) return;
    const high = state.records.filter((record) => record.riskCategory === "high").length;
    const moderate = state.records.filter((record) => record.riskCategory === "moderate").length;
    const low = state.records.filter((record) => record.riskCategory === "low").length;
    const visibleRecords = sortRecords(state.records, state.recordSort)
      .filter((record) => state.recordFilter === "all" || record.riskCategory === state.recordFilter)
      .filter((record) => recordMatches(record, state.recordQuery));
    const rows = visibleRecords.map((record) => `
      <article class="record-card ${recordTone(record)}">
        <div class="record-head">
          <strong>${escapeHtml(record.id)}</strong>
          <span>${escapeHtml(record.classification)}</span>
        </div>
        <div class="record-grid">
          <div><b>${escapeHtml(labelUnknown(record.age))}</b><span>age</span></div>
          <div><b>${escapeHtml(labelUnknown(record.sex))}</b><span>sex</span></div>
          <div><b>${pct(record.abnormalProbability)}</b><span>abnormal</span></div>
          <div><b>${Number(record.riskScore || 0).toFixed(2)}</b><span>risk score</span></div>
          <div><b>${escapeHtml(record.modality || "XRAY")}</b><span>modality</span></div>
          <div><b>${score(record.trabecularBoneScore, 2)}</b><span>DEXA TBS</span></div>
          <div><b>${score(record.corticalThinningIndex, 1)}</b><span>CT cortex</span></div>
          <div><b>${pct(record.marrowDarkRatio)}</b><span>MRI dark</span></div>
        </div>
        <p>${escapeHtml(record.summary)}</p>
        <p>${escapeHtml(record.marrowSignal || "MRI marrow signal not measured")}</p>
        <small>${escapeHtml(labelUnknown(record.date))} · ${escapeHtml(labelUnknown(record.imagePath))}</small>
      </article>
    `).join("");
    recordsPanel.innerHTML = `
      <div class="records-title">
        <div><span>MOONDAY FIST REVIEW</span><h2>Patient Records</h2></div>
        <b>${visibleRecords.length}</b>
      </div>
      <div class="records-source">Source: ${escapeHtml(state.recordSource || "repository data")}</div>
      <div class="records-summary"><span>${high} high</span><span>${moderate} moderate</span><span>${low} low</span></div>
      <div class="records-tools">
        <button data-filter="all" class="${state.recordFilter === "all" ? "active" : ""}">All</button>
        <button data-filter="high" class="${state.recordFilter === "high" ? "active" : ""}">High</button>
        <button data-filter="moderate" class="${state.recordFilter === "moderate" ? "active" : ""}">Moderate</button>
        <button data-filter="low" class="${state.recordFilter === "low" ? "active" : ""}">Low</button>
        <select aria-label="Sort patient records">
          <option value="abnormal" ${state.recordSort === "abnormal" ? "selected" : ""}>Sort: abnormal first</option>
          <option value="risk" ${state.recordSort === "risk" ? "selected" : ""}>Sort: risk score</option>
          <option value="date" ${state.recordSort === "date" ? "selected" : ""}>Sort: newest date</option>
        </select>
        <input value="${escapeHtml(state.recordQuery)}" placeholder="Search id, class, image" aria-label="Search patient records" />
      </div>
      <div class="records-list">${rows || "<p>No spine records match the current filters.</p>"}</div>
    `;
  }

  if (recordsPanel) {
    recordsPanel.addEventListener("click", (event) => {
      const button = event.target.closest("button[data-filter]");
      if (!button) return;
      state.recordFilter = button.dataset.filter;
      renderRecordsPanel();
    });
    recordsPanel.addEventListener("change", (event) => {
      if (!event.target.matches("select")) return;
      state.recordSort = event.target.value;
      renderRecordsPanel();
    });
    recordsPanel.addEventListener("input", (event) => {
      if (!event.target.matches("input")) return;
      state.recordQuery = event.target.value;
      renderRecordsPanel();
      const input = recordsPanel.querySelector("input");
      input?.focus();
      input?.setSelectionRange(state.recordQuery.length, state.recordQuery.length);
    });
  }

  function showRecords(visible) {
    state.recordsVisible = visible;
    if (!recordsPanel) return;
    recordsPanel.hidden = !visible;
    recordsPanel.classList.toggle("show", visible);
    if (visible) renderRecordsPanel();
  }

  function updateHands(hands, time) {
    if (!hands.length) {
      state.targetY += 0.002;
      showRecords(false);
      return;
    }

    const primary = hands[0].landmarks;
    const fist = hands.some((hand) => isGrab(hand.landmarks));
    showRecords(fist);
    if (fist) {
      state.targetZoom = 1.08;
      return;
    }

    const index = primary[8];
    const wrist = primary[0];
    if (isPoint(primary) || isPinch(primary)) {
      state.targetY = (0.5 - index.x) * Math.PI * 1.35;
      state.targetX = (0.5 - index.y) * Math.PI * 0.42;
    } else {
      state.targetY += (index.x - wrist.x) * 0.02;
      state.targetX += (index.y - wrist.y) * 0.012;
    }

    if (isPinch(primary)) state.targetSlice = THREE.MathUtils.clamp(1 - index.y, 0.08, 0.96);

    if (hands.length >= 2) {
      const a = hands[0].landmarks[8];
      const b = hands[1].landmarks[8];
      const distance = Math.hypot(a.x - b.x, a.y - b.y);
      state.targetZoom = THREE.MathUtils.clamp(0.65 + distance * 2.1, 0.72, 1.8);
    } else {
      state.targetZoom = THREE.MathUtils.clamp(1.55 - pinchStrength(primary), 0.78, 1.55);
    }

    if (isPoint(primary) && time - state.lastSelect > 0.75) {
      const next = THREE.MathUtils.clamp(Math.floor((1 - index.x) * CONDITIONS.length), 0, CONDITIONS.length - 1);
      if (next !== state.selected) {
        state.selected = next;
        state.lastSelect = time;
        updatePanel();
      }
    }
  }

  function render(time) {
    state.rotateX += (state.targetX - state.rotateX) * 0.08;
    state.rotateY += (state.targetY - state.rotateY) * 0.08;
    state.zoom += (state.targetZoom - state.zoom) * 0.08;
    state.slice += (state.targetSlice - state.slice) * 0.12;

    root.rotation.x = state.rotateX;
    root.rotation.y = state.rotateY;
    root.scale.setScalar(state.zoom);

    const sliceIndex = Math.round((1 - state.slice) * (vertebrae.length - 1));
    slicePlane.position.y = THREE.MathUtils.lerp(vertebrae[vertebrae.length - 1].position.y, vertebrae[0].position.y, state.slice);
    slicePlane.material.opacity = 0.12 + Math.sin(time * 4) * 0.04;

    vertebrae.forEach((vertebra, i) => {
      const inSlice = Math.abs(i - sliceIndex) <= 1;
      vertebra.userData.body.material.emissive.setHex(inSlice ? 0x17495a : 0x06151b);
      vertebra.scale.setScalar(inSlice ? 1.08 : 1);
    });

    markers.forEach((marker, i) => {
      const active = i === state.selected;
      marker.userData.ring.rotation.z += active ? 0.035 : 0.012;
      marker.userData.ring.scale.setScalar(active ? 1.15 + Math.sin(time * 5) * 0.04 : 0.86);
      marker.userData.pulse.scale.setScalar(active ? 1.3 + Math.sin(time * 7) * 0.22 : 0.7);
      marker.userData.label.visible = active;
    });

    discs.forEach((disc, i) => {
      disc.material.opacity = i >= 12 ? 0.72 : 0.5;
      disc.rotation.z += 0.002;
    });
    grid.material.opacity = 0.18 + Math.sin(time * 0.8) * 0.04;
    composer.render();
  }

  return {
    updateHands,
    render,
    setRecords(records, source) {
      state.records = Array.isArray(records) ? records : [];
      state.recordSource = source || "repository data";
      renderRecordsPanel();
      showRecords(false);
    },
  };
}
