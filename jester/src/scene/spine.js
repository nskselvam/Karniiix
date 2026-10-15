import * as THREE from "three";
import { EffectComposer } from "three/addons/postprocessing/EffectComposer.js";
import { RenderPass } from "three/addons/postprocessing/RenderPass.js";
import { UnrealBloomPass } from "three/addons/postprocessing/UnrealBloomPass.js";
import { OutputPass } from "three/addons/postprocessing/OutputPass.js";
import { GLTFLoader } from "three/addons/loaders/GLTFLoader.js";
import { isGrab, isPinch, isPoint, pinchStrength } from "../hands/gestures.js";

const SPINE_MODEL_URL = "/assets/models/human_spine.glb";
const MODEL_HEIGHT = 8.2;
const VERTEBRA_NAMES = [
  "C1", "C2", "C3", "C4", "C5", "C6", "C7",
  "T1", "T2", "T3", "T4", "T5", "T6", "T7", "T8", "T9", "T10", "T11", "T12",
  "L1", "L2", "L3", "L4", "L5", "SACRUM",
];
const ANATOMY_COUNT = VERTEBRA_NAMES.length;
const LEVEL_LABELS = [
  ["C1", "C1"], ["C4", "C4"], ["C7", "C7"], ["T3", "T3"], ["T6", "T6"], ["T9", "T9"],
  ["T12", "T12"], ["L3", "L3"], ["L5", "L5"], ["S", "SACRUM"],
];

const CONDITIONS = [
  { name: "Compression fracture", level: "T12", region: "Thoracolumbar junction", severity: "High", metric: "38% height loss", problem: "Anterior body collapse wedges T12 and destabilizes the sagittal profile.", action: "Check trauma history, osteoporosis markers, and serial imaging.", color: 0xff8a3d, targets: ["T12"], anchor: "T12", effect: "compress" },
  { name: "Disc degeneration", level: "L4-L5", region: "Lower lumbar", severity: "Moderate", metric: "Grade 3 disc change", problem: "Disc height loss narrows foraminal space and can irritate nerve roots.", action: "Correlate with pain pattern, mobility, and MRI findings.", color: 0xffc857, targets: ["DISC_L4-L5"], anchor: "DISC_L4-L5", effect: "narrow" },
  { name: "Disc herniation", level: "L5-S1", region: "Lumbosacral", severity: "High", metric: "6 mm posterolateral", problem: "Nucleus material bulges past the annulus toward the exiting S1 root.", action: "Assess sciatica, straight-leg raise, and MRI for extrusion.", color: 0xff5bd1, targets: ["DISC_L5-S1"], anchor: "DISC_L5-S1", effect: "bulge" },
  { name: "Spondylolisthesis", level: "L5 on S1", region: "Lumbosacral", severity: "Moderate", metric: "Grade 1 slip", problem: "L5 has slipped forward over the sacrum, stretching the posterior elements.", action: "Flexion-extension X-rays to check instability.", color: 0xa98bff, targets: ["L5"], anchor: "L5", effect: "slip" },
  { name: "Nerve root impingement", level: "L4 right", region: "Right L4-L5 foramen", severity: "Moderate", metric: "Foraminal narrowing", problem: "The exiting L4 root is pinched by disc height loss and facet overgrowth.", action: "Review dermatomal pain, reflexes, and motor strength.", color: 0x5ee7ff, targets: ["L4"], anchor: "L4", effect: "nerve" },
  { name: "Spinal stenosis", level: "L3-L4", region: "Central canal", severity: "Moderate", metric: "Canal 9 mm AP", problem: "Thickened ligament and facets narrow the central canal around the cauda equina.", action: "Assess neurogenic claudication and walking tolerance.", color: 0x4dffb0, targets: ["L3", "DISC_L3-L4"], anchor: "DISC_L3-L4", effect: "stenosis" },
  { name: "Cervical spondylosis", level: "C5-C6", region: "Lower cervical", severity: "Low", metric: "Anterior osteophytes", problem: "Bone spurs and disc wear stiffen the neck and may crowd cervical roots.", action: "Check neck range of motion and arm paresthesia.", color: 0xc6ff4d, targets: ["C5", "C6", "DISC_C5-C6"], anchor: "DISC_C5-C6", effect: "spurs" },
  { name: "Scoliosis curve", level: "T8-L3", region: "Thoracic to lumbar", severity: "High", metric: "Cobb 31 deg", problem: "Lateral curvature shifts load across the discs and facet joints.", action: "Track curve progression, posture loading, and orthopedic follow-up.", color: 0xff4d5e, range: ["T8", "L3"], anchor: "T10", effect: "scoliosis" },
];
const BONE_COLOR = new THREE.Color(0xe6dccb);
const DISC_COLOR = new THREE.Color(0x8fa6b8);
const SEVERITY_RANK = { High: 0, Moderate: 1, Low: 2 };
const SEVERITY_FILTERS = ["All", "High", "Moderate", "Low"];
const TOOLBAR_ACTIONS = [
  ["view-front", "Front", "1"], ["view-side", "Side", "2"], ["view-back", "Back", "3"],
  ["auto-rotate", "Auto rotate", "A"], ["labels", "Labels", "L"], ["isolate", "Isolate", "I"],
  ["normal", "Compare normal", "N"], ["reset", "Reset", "0"], ["snapshot", "Snapshot", "S"],
];
const ONE = new THREE.Vector3(1, 1, 1);

const hex = (color) => `#${color.toString(16).padStart(6, "0")}`;

function describeLevel(name) {
  if (name.startsWith("DISC_")) return `${name.slice(5)} intervertebral disc`;
  if (name === "SACRUM") return "Sacrum";
  const region = { C: "Cervical", T: "Thoracic", L: "Lumbar" }[name[0]];
  return `${name} · ${region} vertebra`;
}

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
  canvas.width = 640;
  canvas.height = 128;
  const ctx = canvas.getContext("2d");
  ctx.fillStyle = "rgba(3, 7, 10, 0.72)";
  ctx.strokeStyle = color;
  ctx.lineWidth = 2;
  roundedRect(ctx, 10, 18, 620, 80, 10);
  ctx.fill();
  ctx.stroke();
  ctx.fillStyle = color;
  ctx.textAlign = "center";
  ctx.textBaseline = "middle";
  let fontSize = 38;
  ctx.font = `700 ${fontSize}px Rajdhani, sans-serif`;
  while (fontSize > 18 && ctx.measureText(text).width > 584) {
    fontSize -= 1;
    ctx.font = `700 ${fontSize}px Rajdhani, sans-serif`;
  }
  ctx.fillText(text, 320, 59);
  const texture = new THREE.CanvasTexture(canvas);
  const sprite = new THREE.Sprite(new THREE.SpriteMaterial({ map: texture, transparent: true, depthWrite: false }));
  sprite.scale.set(2.4, 0.48, 1);
  return sprite;
}

function makeLevelTag(text) {
  const canvas = document.createElement("canvas");
  canvas.width = 128;
  canvas.height = 48;
  const ctx = canvas.getContext("2d");
  ctx.font = "700 28px Arial, sans-serif";
  ctx.fillStyle = "#aeb8c7";
  ctx.textAlign = "right";
  ctx.textBaseline = "middle";
  ctx.fillText(text, 120, 24);
  const texture = new THREE.CanvasTexture(canvas);
  const sprite = new THREE.Sprite(new THREE.SpriteMaterial({ map: texture, transparent: true, depthWrite: false }));
  sprite.scale.set(0.46, 0.17, 1);
  return sprite;
}

function glow(color, opacity) {
  return new THREE.MeshBasicMaterial({ color, transparent: true, opacity, blending: THREE.AdditiveBlending, depthWrite: false });
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

export function createSpineScene(container, panel, recordsPanel, controls = {}) {
  const scene = new THREE.Scene();

  const camera = new THREE.PerspectiveCamera(68, 1, 0.1, 100);
  camera.position.set(0, 0, 14);
  camera.lookAt(0, 0, 0);

  const renderer = new THREE.WebGLRenderer({ antialias: true, alpha: true });
  renderer.setPixelRatio(Math.min(devicePixelRatio, 2));
  renderer.setClearColor(0x03070a, 1);
  renderer.toneMapping = THREE.ACESFilmicToneMapping;
  renderer.toneMappingExposure = 1;
  container.appendChild(renderer.domElement);

  const composer = new EffectComposer(renderer);
  composer.addPass(new RenderPass(scene, camera));
  const bloom = new UnrealBloomPass(new THREE.Vector2(1, 1), 0.3, 0.36, 0.72);
  composer.addPass(bloom);
  composer.addPass(new OutputPass());

  scene.add(new THREE.HemisphereLight(0x9eefff, 0x09121a, 1.35));
  const key = new THREE.DirectionalLight(0xffffff, 2.3);
  key.position.set(3, 4, 5);
  scene.add(key);

  const root = new THREE.Group();
  scene.add(root);

  const levels = [];
  const levelsByName = new Map();
  const vertebrae = [];
  const discs = [];
  const levelTags = [];
  const spineModel = new THREE.Group();
  root.add(spineModel);

  const conditionColors = CONDITIONS.map((condition) => new THREE.Color(condition.color));
  const markers = CONDITIONS.map((condition) => {
    const marker = new THREE.Group();
    const ring = new THREE.Mesh(new THREE.TorusGeometry(0.22, 0.012, 8, 56), glow(condition.color, 0.7));
    const pulse = new THREE.Mesh(new THREE.SphereGeometry(0.05, 16, 10), glow(condition.color, 0.95));
    marker.add(ring, pulse);
    marker.visible = false;
    const label = makeLabel(`${condition.level} · ${condition.name}`, `#${condition.color.toString(16).padStart(6, "0")}`);
    label.scale.set(2.0, 0.4, 1);
    label.visible = false;
    const leader = new THREE.Line(
      new THREE.BufferGeometry().setFromPoints([new THREE.Vector3(), new THREE.Vector3(), new THREE.Vector3()]),
      new THREE.LineBasicMaterial({ color: condition.color, transparent: true, opacity: 0.6 }),
    );
    leader.visible = false;
    marker.userData = { condition, ring, pulse, label, leader, extras: [] };
    root.add(marker, label, leader);
    return marker;
  });

  const slicePlane = new THREE.Mesh(new THREE.BoxGeometry(5, 0.018, 0.02), glow(0xd8e8ff, 0.12));
  slicePlane.visible = false;
  root.add(slicePlane);

  function addExtra(conditionIndex, object) {
    object.userData.baseOpacity = object.material.opacity;
    markers[conditionIndex].userData.extras.push(object);
    root.add(object);
  }

  function applyPathology(conditionIndex, meshesByName) {
    const condition = CONDITIONS[conditionIndex];
    const color = condition.color;
    const targets = condition.targets.map((name) => meshesByName.get(name)).filter(Boolean);
    const anchor = meshesByName.get(condition.anchor);
    if (!anchor) return;
    const box = anchor.geometry.boundingBox;
    const w = box.max.x - box.min.x;
    const h = box.max.y - box.min.y;
    const d = box.max.z - box.min.z;
    const p = anchor.position;

    if (condition.effect === "compress") {
      anchor.userData.baseScale.y = 0.62;
      anchor.rotation.x = 0.12;
    } else if (condition.effect === "narrow") {
      anchor.userData.baseScale.set(1.08, 0.4, 1.08);
    } else if (condition.effect === "bulge") {
      const bulge = new THREE.Mesh(
        new THREE.SphereGeometry(1, 24, 16),
        new THREE.MeshStandardMaterial({ color, emissive: color, emissiveIntensity: 0.45, roughness: 0.4, transparent: true, opacity: 0.92 }),
      );
      bulge.scale.set(w * 0.2, h * 0.75, d * 0.18);
      bulge.position.set(p.x + w * 0.28, p.y, p.z - d * 0.32);
      addExtra(conditionIndex, bulge);
    } else if (condition.effect === "slip") {
      anchor.position.z += d * 0.18;
    } else if (condition.effect === "nerve") {
      const start = new THREE.Vector3(p.x + w * 0.3, p.y - h * 0.35, p.z - d * 0.15);
      const curve = new THREE.CatmullRomCurve3([
        start,
        new THREE.Vector3(p.x + w * 0.7, p.y - h * 0.9, p.z),
        new THREE.Vector3(p.x + w * 1.05, p.y - h * 2.2, p.z + d * 0.15),
        new THREE.Vector3(p.x + w * 1.3, p.y - h * 3.6, p.z + d * 0.25),
      ]);
      const nerve = new THREE.Mesh(new THREE.TubeGeometry(curve, 48, 0.022, 10, false), glow(color, 0.85));
      const pinch = new THREE.Mesh(new THREE.SphereGeometry(0.06, 16, 10), glow(0xff4d5e, 0.95));
      pinch.position.copy(start);
      addExtra(conditionIndex, nerve);
      addExtra(conditionIndex, pinch);
    } else if (condition.effect === "stenosis") {
      const disc = meshesByName.get("DISC_L3-L4") || anchor;
      const canal = new THREE.Mesh(new THREE.TorusGeometry(w * 0.16, 0.022, 10, 40), glow(color, 0.85));
      canal.rotation.x = Math.PI / 2;
      canal.scale.set(1, 0.55, 1);
      canal.position.set(disc.position.x, disc.position.y, disc.position.z - d * 0.85);
      addExtra(conditionIndex, canal);
      for (const side of [-1, 1]) {
        const pinch = new THREE.Mesh(new THREE.ConeGeometry(0.03, 0.1, 10), glow(color, 0.9));
        pinch.rotation.z = side * Math.PI / 2;
        pinch.position.set(canal.position.x + side * (w * 0.16 + 0.06), canal.position.y, canal.position.z);
        addExtra(conditionIndex, pinch);
      }
    } else if (condition.effect === "spurs") {
      for (const mesh of targets.filter((mesh) => !mesh.name.startsWith("DISC_"))) {
        const mb = mesh.geometry.boundingBox;
        const mw = mb.max.x - mb.min.x;
        const mh = mb.max.y - mb.min.y;
        for (const sx of [-1, 1]) {
          for (const sy of [-1, 1]) {
            const spur = new THREE.Mesh(
              new THREE.ConeGeometry(0.025, 0.09, 8),
              new THREE.MeshStandardMaterial({ color, emissive: color, emissiveIntensity: 0.5, transparent: true, opacity: 0.95 }),
            );
            spur.rotation.z = sx * -Math.PI / 2 + sy * 0.4 * sx;
            spur.position.set(mesh.position.x + sx * mw * 0.22, mesh.position.y + sy * mh * 0.3, mesh.position.z + (mb.max.z - mb.min.z) * 0.3);
            addExtra(conditionIndex, spur);
          }
        }
      }
    }
  }

  function buildSpine(gltf) {
    const meshes = [];
    gltf.scene.updateMatrixWorld(true);
    gltf.scene.traverse((node) => {
      if (node.isMesh) meshes.push(node);
    });

    const bounds = new THREE.Box3();
    meshes.forEach((mesh) => bounds.expandByObject(mesh));
    const size = bounds.getSize(new THREE.Vector3());
    const center = bounds.getCenter(new THREE.Vector3());
    const scale = MODEL_HEIGHT / size.y;
    const toModel = new THREE.Matrix4()
      .makeScale(scale, scale, scale)
      .multiply(new THREE.Matrix4().makeTranslation(-center.x, -center.y, -center.z));

    const meshesByName = new Map();
    for (const source of meshes) {
      const geometry = source.geometry.clone().applyMatrix4(source.matrixWorld).applyMatrix4(toModel);
      geometry.computeBoundingBox();
      const pivot = geometry.boundingBox.getCenter(new THREE.Vector3());
      geometry.translate(-pivot.x, -pivot.y, -pivot.z);
      geometry.computeBoundingBox();
      geometry.computeVertexNormals();

      const isDisc = source.name.startsWith("DISC_");
      const material = isDisc
        ? new THREE.MeshStandardMaterial({ color: DISC_COLOR, roughness: 0.55, transparent: true, opacity: 0.9, emissive: 0x0b1620 })
        : new THREE.MeshStandardMaterial({ color: BONE_COLOR, roughness: 0.58, metalness: 0.02, emissive: 0x090b11, transparent: true });
      const mesh = new THREE.Mesh(geometry, material);
      mesh.name = source.name;
      mesh.position.copy(pivot);
      mesh.userData = { level: source.name, disc: isDisc, baseColor: isDisc ? DISC_COLOR : BONE_COLOR, baseScale: new THREE.Vector3(1, 1, 1), conditions: [] };
      spineModel.add(mesh);
      (isDisc ? discs : vertebrae).push(mesh);
      meshesByName.set(source.name, mesh);
    }
    const allMeshes = [...vertebrae, ...discs];
    for (const mesh of allMeshes) mesh.userData.normal = { position: mesh.position.clone(), quaternion: mesh.quaternion.clone() };

    CONDITIONS.forEach((condition, index) => {
      if (condition.range) {
        const [top, bottom] = condition.range.map((name) => meshesByName.get(name).position.y);
        condition.targets = allMeshes
          .filter((mesh) => mesh.position.y <= top + 1e-6 && mesh.position.y >= bottom - 1e-6)
          .map((mesh) => mesh.name);
        if (condition.effect === "scoliosis") {
          const span = top - bottom;
          const amplitude = 0.3;
          for (const mesh of allMeshes) {
            const t = (mesh.position.y - bottom) / span;
            if (t < -0.15 || t > 1.15) continue;
            const clamped = THREE.MathUtils.clamp(t, 0, 1);
            mesh.position.x += amplitude * Math.sin(Math.PI * clamped);
            mesh.rotation.z = -Math.atan((amplitude * Math.PI / span) * Math.cos(Math.PI * clamped));
          }
        }
      }
      condition.targets.forEach((name) => meshesByName.get(name)?.userData.conditions.push(index));
    });
    CONDITIONS.forEach((condition, index) => applyPathology(index, meshesByName));
    for (const mesh of allMeshes) mesh.userData.pathology = { position: mesh.position.clone(), quaternion: mesh.quaternion.clone() };

    for (const mesh of allMeshes) {
      const box = mesh.geometry.boundingBox;
      const level = {
        name: mesh.name,
        disc: mesh.userData.disc,
        y: mesh.position.y,
        x: mesh.position.x,
        z: mesh.position.z,
        width: box.max.x - box.min.x,
        depth: box.max.z - box.min.z,
      };
      levels.push(level);
      levelsByName.set(mesh.name, level);
    }
    levels.sort((a, b) => a.y - b.y);

    const leftEdge = -size.x * scale * 0.5;
    for (const [label, name] of LEVEL_LABELS) {
      const level = levelsByName.get(name);
      if (!level) continue;
      const tag = makeLevelTag(label);
      tag.position.set(leftEdge - 0.55, level.y, level.z + 0.3);
      const tick = new THREE.Line(
        new THREE.BufferGeometry().setFromPoints([
          new THREE.Vector3(leftEdge - 0.12, level.y, level.z + 0.3),
          new THREE.Vector3(level.x - level.width * 0.5 - 0.04, level.y, level.z + 0.3),
        ]),
        new THREE.LineBasicMaterial({ color: 0x626b78, transparent: true, opacity: 0.7 }),
      );
      root.add(tag, tick);
      levelTags.push({ name, tag, tick });
    }

    const labelX = Math.max(...levels.map((level) => level.x + level.width * 0.5)) + 1.45;
    const order = markers
      .map((marker) => ({ marker, level: levelsByName.get(marker.userData.condition.anchor) }))
      .filter((item) => item.level)
      .sort((a, b) => a.level.y - b.level.y);
    const gap = 0.5;
    let floor = -Infinity;
    for (const item of order) {
      item.labelY = Math.max(item.level.y, floor + gap);
      floor = item.labelY;
    }
    for (const item of order) {
      const { marker, level } = item;
      const front = level.z + level.depth * 0.5 + 0.02;
      marker.position.set(level.x, level.y, front);
      marker.visible = true;
      const { label, leader } = marker.userData;
      label.position.set(labelX, item.labelY, front);
      label.visible = true;
      leader.geometry.setFromPoints([
        new THREE.Vector3(level.x + level.width * 0.5, level.y, front),
        new THREE.Vector3(labelX - 1.35, item.labelY, front),
        new THREE.Vector3(labelX - 1.05, item.labelY, front),
      ]);
      leader.visible = true;
    }
    slicePlane.visible = true;
    state.built = true;
  }

  new GLTFLoader().load(
    SPINE_MODEL_URL,
    buildSpine,
    undefined,
    (error) => console.error("Failed to load spinal column model", error),
  );

  const state = { rotateX: 0, rotateY: 0, targetX: 0, targetY: 0, zoom: 1.7, targetZoom: 1.7, slice: 0.72, targetSlice: 0.72, selected: 0, lastSelect: 0, records: [], recordSource: "", recordsVisible: false, recordFilter: "all", recordSort: "abnormal", recordQuery: "", mouseDown: false, mouseMoved: false, mouseX: 0, mouseY: 0, built: false, autoRotate: true, wobble: 1, showLabels: true, isolate: false, normalView: false, normalMix: 0, severity: "All", hovered: null, hoverAt: null, lastHover: 0 };

  function resize() {
    const width = innerWidth;
    const height = innerHeight;
    renderer.setSize(width, height);
    composer.setSize(width, height);
    bloom.setSize(width, height);
    camera.aspect = width / height;
    camera.updateProjectionMatrix();
    root.position.x = width > 900 ? -1.6 : 0;
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
  const tooltip = document.createElement("div");
  tooltip.id = "spine-tooltip";
  tooltip.hidden = true;
  document.body.appendChild(tooltip);

  canvas.addEventListener("pointerleave", () => {
    state.hoverAt = null;
    state.hovered = null;
    tooltip.hidden = true;
  });
  canvas.addEventListener("pointermove", (event) => {
    if (!state.mouseDown) {
      state.hoverAt = { x: event.offsetX, y: event.offsetY, clientX: event.clientX, clientY: event.clientY };
      return;
    }
    tooltip.hidden = true;
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
    if (!state.mouseMoved) selectAt(event.offsetX, event.offsetY);
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

  const raycaster = new THREE.Raycaster();
  function selectAt(x, y) {
    const pointer = new THREE.Vector2((x / canvas.clientWidth) * 2 - 1, -(y / canvas.clientHeight) * 2 + 1);
    raycaster.setFromCamera(pointer, camera);
    const pickables = [
      ...vertebrae,
      ...discs,
      ...markers.flatMap((marker) => [marker.userData.label, marker.userData.ring, ...marker.userData.extras]),
    ];
    for (const hit of raycaster.intersectObjects(pickables, false)) {
      const markerIndex = markers.findIndex((marker) =>
        marker.userData.label === hit.object || marker.userData.ring === hit.object || marker.userData.extras.includes(hit.object));
      const visible = visibleSet();
      const options = (markerIndex >= 0 ? [markerIndex] : hit.object.userData.conditions || []).filter((index) => visible.has(index));
      if (!options.length) continue;
      const current = options.indexOf(state.selected);
      select(options[(current + 1) % options.length]);
      return;
    }
  }

  function updateHover(time) {
    if (!state.hoverAt || state.mouseDown || time - state.lastHover < 0.06) return;
    state.lastHover = time;
    const { x, y, clientX, clientY } = state.hoverAt;
    raycaster.setFromCamera(new THREE.Vector2((x / canvas.clientWidth) * 2 - 1, -(y / canvas.clientHeight) * 2 + 1), camera);
    const hit = raycaster.intersectObjects([...vertebrae, ...discs], false).find((item) => item.object.material.opacity > 0.4);
    state.hovered = hit?.object || null;
    if (!state.hovered) {
      tooltip.hidden = true;
      return;
    }
    const visible = visibleSet();
    const problems = state.normalView ? [] : state.hovered.userData.conditions.filter((index) => visible.has(index));
    tooltip.innerHTML = `<b>${describeLevel(state.hovered.name)}</b>${problems.length
      ? problems.map((index) => `<span style="--tone:${hex(CONDITIONS[index].color)}">${CONDITIONS[index].name} · ${CONDITIONS[index].severity}</span>`).join("")
      : `<span class="ok">${state.normalView ? "Healthy reference alignment" : "No problem flagged"}</span>`}`;
    tooltip.style.left = `${Math.min(clientX + 16, innerWidth - 260)}px`;
    tooltip.style.top = `${clientY + 16}px`;
    tooltip.hidden = false;
  }

  function visibleSet() {
    return new Set(CONDITIONS.map((condition, index) => index)
      .filter((index) => state.severity === "All" || CONDITIONS[index].severity === state.severity));
  }

  function orderedVisible() {
    const visible = visibleSet();
    return CONDITIONS.map((condition, index) => index)
      .filter((index) => visible.has(index))
      .sort((a, b) => SEVERITY_RANK[CONDITIONS[a].severity] - SEVERITY_RANK[CONDITIONS[b].severity] || a - b);
  }

  function select(index) {
    if (index === state.selected) return;
    state.selected = index;
    updatePanel();
  }

  function step(direction) {
    const order = orderedVisible();
    if (!order.length) return;
    const current = order.indexOf(state.selected);
    select(order[(current + direction + order.length) % order.length]);
  }

  function setSeverity(severity) {
    state.severity = severity;
    const order = orderedVisible();
    if (order.length && !order.includes(state.selected)) state.selected = order[0];
    updatePanel();
  }

  panel.addEventListener("click", (event) => {
    const filter = event.target.closest("[data-severity]");
    if (filter) return setSeverity(filter.dataset.severity);
    const nav = event.target.closest("[data-step]");
    if (nav) return step(Number(nav.dataset.step));
    const item = event.target.closest("[data-condition]");
    if (item) select(Number(item.dataset.condition));
  });

  function updatePanel() {
    const condition = CONDITIONS[state.selected];
    const order = orderedVisible();
    const position = order.indexOf(state.selected);
    panel.innerHTML = `
      <div class="spine-head">
        <div class="spine-kicker">KARNIX SPINE ANALYSIS</div>
        <div class="spine-nav">
          <button type="button" data-step="-1" aria-label="Previous problem">&#8249;</button>
          <span>${position >= 0 ? position + 1 : 0}/${order.length}</span>
          <button type="button" data-step="1" aria-label="Next problem">&#8250;</button>
        </div>
      </div>
      ${state.normalView ? `<div class="spine-normal-note">Showing healthy reference alignment. Turn off Compare normal to see the problems.</div>` : ""}
      <h2 style="color:#${condition.color.toString(16).padStart(6, "0")}">${condition.name}</h2>
      <div class="spine-meta"><span>${condition.level}</span><span>${condition.region}</span><span>${condition.severity}</span><span>${condition.metric}</span></div>
      <p>${condition.problem}</p>
      <p>${condition.action}</p>
      <div class="spine-stats">
        <div><b>${CONDITIONS.length}</b><span>problems found</span></div>
        <div><b>${CONDITIONS.filter((item) => item.severity === "High").length}</b><span>high risk</span></div>
        <div><b>${ANATOMY_COUNT}</b><span>vertebrae shown</span></div>
      </div>
      <div class="spine-filter">
        ${SEVERITY_FILTERS.map((severity) => `<button type="button" data-severity="${severity}" class="${state.severity === severity ? "active" : ""}">${severity}<small>${severity === "All" ? CONDITIONS.length : CONDITIONS.filter((item) => item.severity === severity).length}</small></button>`).join("")}
      </div>
      <ul class="spine-problems">
        ${order.map((index) => ({ item: CONDITIONS[index], index }))
          .map(({ item, index }) => `
          <li data-condition="${index}" class="${index === state.selected ? "active" : ""}" style="--tone:#${item.color.toString(16).padStart(6, "0")}">
            <i></i><span>${item.name}</span><em>${item.level}</em><b class="sev-${item.severity.toLowerCase()}">${item.severity}</b>
          </li>`).join("")}
      </ul>
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
        <div><span>MOONDAY RECORD REVIEW</span><h2>Patient Records</h2></div>
        <b>${visibleRecords.length}</b>
        <button type="button" class="records-close" data-action="close-records" aria-label="Close patient records">&times;</button>
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
      if (event.target.closest("[data-action='close-records']")) return showRecords(false);
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
    updateToolbar();
  }

  const recordsButton = controls.recordsButton;
  recordsButton?.addEventListener("click", () => showRecords(!state.recordsVisible));

  function setView(angle) {
    const turn = Math.PI * 2;
    state.autoRotate = false;
    state.targetY = Math.round((state.targetY - angle) / turn) * turn + angle;
    state.targetX = 0;
    updateToolbar();
  }

  function snapshot() {
    composer.render();
    const link = document.createElement("a");
    link.download = `spine-${CONDITIONS[state.selected].level.replace(/\s+/g, "-")}-${Date.now()}.png`;
    link.href = renderer.domElement.toDataURL("image/png");
    link.click();
  }

  function runAction(action) {
    if (action === "view-front") setView(0);
    else if (action === "view-side") setView(Math.PI / 2);
    else if (action === "view-back") setView(Math.PI);
    else if (action === "auto-rotate") state.autoRotate = !state.autoRotate;
    else if (action === "labels") state.showLabels = !state.showLabels;
    else if (action === "isolate") state.isolate = !state.isolate;
    else if (action === "normal") {
      state.normalView = !state.normalView;
      updatePanel();
    } else if (action === "reset") {
      const turn = Math.PI * 2;
      Object.assign(state, { targetX: 0, targetY: Math.round(state.targetY / turn) * turn, targetZoom: 1.7, targetSlice: 0.72, autoRotate: true, showLabels: true, isolate: false, normalView: false });
      setSeverity("All");
    } else if (action === "snapshot") snapshot();
    else if (action === "records") showRecords(!state.recordsVisible);
    updateToolbar();
  }

  const toolbar = controls.toolbar;
  if (toolbar) {
    toolbar.innerHTML = TOOLBAR_ACTIONS.map(([action, text, key]) =>
      `<button type="button" data-action="${action}" title="${text} (${key})">${text}</button>`).join("");
    toolbar.addEventListener("click", (event) => {
      const button = event.target.closest("[data-action]");
      if (button) runAction(button.dataset.action);
    });
  }

  function updateToolbar() {
    const toggles = { "auto-rotate": state.autoRotate, labels: state.showLabels, isolate: state.isolate, normal: state.normalView };
    toolbar?.querySelectorAll("[data-action]").forEach((button) => {
      button.classList.toggle("active", Boolean(toggles[button.dataset.action]));
    });
    if (recordsButton) {
      recordsButton.classList.toggle("active", state.recordsVisible);
      recordsButton.textContent = `PATIENT RECORDS · ${state.records.length}`;
    }
  }

  updateToolbar();

  const KEYS = { "1": "view-front", "2": "view-side", "3": "view-back", a: "auto-rotate", l: "labels", i: "isolate", n: "normal", "0": "reset", s: "snapshot", r: "records" };
  addEventListener("keydown", (event) => {
    if (event.metaKey || event.ctrlKey || event.altKey || event.target.closest?.("input, select, textarea")) return;
    const key = event.key.length === 1 ? event.key.toLowerCase() : event.key;
    if (key === "ArrowRight" || key === "ArrowDown") step(1);
    else if (key === "ArrowLeft" || key === "ArrowUp") step(-1);
    else if (key === "Escape") showRecords(false);
    else if (KEYS[key]) runAction(KEYS[key]);
    else return;
    event.preventDefault();
  });

  function updateHands(hands, time) {
    if (!hands.length) {
      if (state.autoRotate) state.targetY += 0.002;
      return;
    }

    const primary = hands[0].landmarks;
    // A closed fist freezes the current view so it can be inspected hands-free.
    if (hands.some((hand) => isGrab(hand.landmarks))) return;

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
      const order = orderedVisible();
      const next = order[THREE.MathUtils.clamp(Math.floor((1 - index.x) * order.length), 0, order.length - 1)] ?? state.selected;
      if (next !== state.selected) {
        state.lastSelect = time;
        select(next);
      }
    }
  }

  function render(time) {
    state.rotateX += (state.targetX - state.rotateX) * 0.08;
    state.rotateY += (state.targetY - state.rotateY) * 0.08;
    state.zoom += (state.targetZoom - state.zoom) * 0.08;
    state.slice += (state.targetSlice - state.slice) * 0.12;

    root.rotation.x = state.rotateX;
    state.wobble += ((state.autoRotate ? 1 : 0) - state.wobble) * 0.05;
    state.normalMix += ((state.normalView ? 1 : 0) - state.normalMix) * 0.1;
    root.rotation.y = state.rotateY + Math.sin(time * 0.35) * 0.244 * state.wobble;
    updateHover(time);
    root.scale.setScalar(state.zoom);

    if (levels.length) {
      const sliceIndex = Math.round((1 - state.slice) * (levels.length - 1));
      slicePlane.position.y = levels[sliceIndex].y;
    }
    slicePlane.material.opacity = 0.04 + Math.sin(time * 4) * 0.015;

    const pulse = 0.5 + Math.sin(time * 3.5) * 0.5;
    const tint = new THREE.Color();
    const pathologyShown = 1 - state.normalMix;
    const visible = visibleSet();
    const focus = CONDITIONS[state.selected];
    const focusNames = new Set([...(focus.targets || []), focus.anchor]);
    for (const mesh of [...vertebrae, ...discs]) {
      const { conditions, baseColor, baseScale, normal, pathology, disc } = mesh.userData;
      if (normal && pathology) {
        mesh.position.lerpVectors(pathology.position, normal.position, state.normalMix);
        mesh.quaternion.slerpQuaternions(pathology.quaternion, normal.quaternion, state.normalMix);
      }
      mesh.scale.copy(baseScale).lerp(ONE, state.normalMix);
      mesh.material.color.copy(baseColor);
      mesh.material.emissive.setHex(disc ? 0x0b1620 : 0x090b11);
      const shownConditions = conditions.filter((index) => visible.has(index));
      const active = shownConditions.includes(state.selected);
      const shown = active ? state.selected : shownConditions[0];
      if (shown !== undefined && pathologyShown > 0.01) {
        const tone = conditionColors[shown];
        mesh.material.color.lerp(tone, (active ? 0.6 : 0.42) * pathologyShown);
        mesh.material.emissive.copy(tint.copy(tone).multiplyScalar((active ? 0.22 + pulse * 0.28 : 0.1) * pathologyShown));
        if (active) mesh.scale.multiplyScalar(1 + pulse * 0.03 * pathologyShown);
      }
      if (mesh === state.hovered) mesh.material.emissive.add(tint.setHex(0x1d4250));
      const targetOpacity = state.isolate && !focusNames.has(mesh.name) ? 0.1 : disc ? 0.9 : 1;
      mesh.material.opacity += (targetOpacity - mesh.material.opacity) * 0.15;
    }

    markers.forEach((marker, i) => {
      const active = i === state.selected;
      const { ring, label, leader, extras } = marker.userData;
      const shown = state.built && visible.has(i) && pathologyShown > 0.05 && (!state.isolate || active);
      marker.visible = shown;
      label.visible = shown && state.showLabels;
      leader.visible = shown && state.showLabels;
      extras.forEach((extra) => { extra.visible = shown; });
      ring.rotation.z += active ? 0.02 : 0.004;
      ring.scale.setScalar(active ? 1.15 + pulse * 0.2 : 0.75);
      marker.userData.pulse.scale.setScalar(active ? 1 + pulse * 0.5 : 0.6);
      label.material.opacity = active ? 1 : 0.62;
      label.scale.set(active ? 2.4 : 2.0, active ? 0.48 : 0.4, 1);
      leader.material.opacity = active ? 0.95 : 0.4;
      extras.forEach((extra) => {
        extra.material.opacity = extra.userData.baseOpacity * (active ? 0.75 + pulse * 0.25 : 0.7) * pathologyShown;
      });
    });

    levelTags.forEach(({ name, tag, tick }) => {
      const focused = !state.normalView && CONDITIONS[state.selected].targets?.includes(name);
      tag.material.color.setHex(focused ? CONDITIONS[state.selected].color : 0xaeb8c7);
      tick.material.color.setHex(focused ? CONDITIONS[state.selected].color : 0x626b78);
      tick.material.opacity = focused ? 0.95 : 0.7;
    });
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
    runAction,
  };
}
