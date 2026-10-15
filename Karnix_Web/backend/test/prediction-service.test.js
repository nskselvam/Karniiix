import assert from "node:assert/strict";
import { chmod, mkdtemp, mkdir, rm, symlink, writeFile } from "node:fs/promises";
import os from "node:os";
import path from "node:path";
import { afterEach, beforeEach, describe, test } from "node:test";
import { buildApp } from "../src/app.js";
import { FakeDatabase } from "./fake-database.js";

const baseConfig = {
  nodeEnv: "test",
  host: "127.0.0.1",
  port: 4000,
  databaseUrl: "test",
  databaseSsl: false,
  corsOrigin: "http://localhost:3000",
  jwtSecret: "test-secret-with-at-least-thirty-two-characters",
  jwtIssuer: "karnix-auth-api",
  jwtAudience: "karnix-web",
  accessTokenTtl: "15m",
  accessTokenMaxAge: 900,
  refreshTokenTtlDays: 7,
  bcryptRounds: 4,
  authRateLimit: 100,
  bodyLimit: 1024,
  trustProxy: false,
  cookieSecure: false,
};

const validOutput = {
  disease: "brain_tumor",
  model: "brain_tumour_cnn",
  modality: "MRI",
  class: "pituitary_tumor",
  probability: 0.91,
  probabilities: {
    glioma_tumor: 0.02,
    meningioma_tumor: 0.03,
    no_tumor: 0.04,
    pituitary_tumor: 0.91,
  },
  research_only: true,
  diagnosis: false,
};

function cookieHeader(response) {
  const values = response.headers["set-cookie"] ?? [];
  return (Array.isArray(values) ? values : [values])
    .map((value) => value.split(";", 1)[0])
    .join("; ");
}

async function register(app) {
  const response = await app.inject({
    method: "POST",
    url: "/api/v1/auth/register",
    headers: { origin: baseConfig.corsOrigin },
    payload: {
      name: "Research User",
      email: "research@example.com",
      password: "StrongPass1",
      confirmPassword: "StrongPass1",
    },
  });
  assert.equal(response.statusCode, 201);
  return cookieHeader(response);
}

describe("research prediction API", () => {
  let directory;
  let app;
  let config;
  let database;

  beforeEach(async () => {
    directory = await mkdtemp(path.join(os.tmpdir(), "karnix-prediction-test-"));
    const inputRoot = path.join(directory, "inputs");
    await mkdir(inputRoot);
    const executable = path.join(directory, "brain-tumor-runner");
    const modelPath = path.join(directory, "brain-tumor.weights");
    const imagePath = path.join(inputRoot, "study.jpg");
    const demoImage = path.join(directory, "demo.jpg");
    await writeFile(modelPath, "test checkpoint");
    const jpegHeader = Buffer.from([0xff, 0xd8, 0xff, 0xd9]);
    await writeFile(imagePath, jpegHeader);
    await writeFile(demoImage, jpegHeader);
    await writeRunner(executable, validOutput);
    config = {
      ...baseConfig,
      predictionInputRoot: inputRoot,
      brainTumorExecutable: executable,
      brainTumorModelPath: modelPath,
      demoBrainTumorImage: demoImage,
    };
    database = new FakeDatabase();
    app = await buildApp({
      config,
      database,
      logger: false,
    });
  });

  afterEach(async () => {
    if (app) await app.close();
    if (directory) await rm(directory, { recursive: true, force: true });
  });

  test("catalog reports configured assets without claiming runtime compatibility", async () => {
    const response = await app.inject({ method: "GET", url: "/api/v1/predictions/catalog" });
    assert.equal(response.statusCode, 200);
    const diseases = response.json().data.diseases;
    const brainTumor = diseases.find((disease) => disease.id === "brain_tumor");
    const unavailable = diseases.find((disease) => disease.id === "diabetes");
    const osteoporosis = diseases.find((disease) => disease.id === "osteoporosis");

    assert.equal(brainTumor.status, "configured");
    assert.equal(brainTumor.readiness, "compatibility_not_verified");
    assert.match(brainTumor.checkpoint_sha256, /^[a-f0-9]{64}$/);
    assert.equal(brainTumor.model_id, "brain_tumour_cnn");
    assert.equal(brainTumor.diagnosis, false);
    assert.equal(unavailable.status, "unavailable");
    assert.equal(osteoporosis.status, "unavailable");
    assert.equal(osteoporosis.modality, "dexa");
    assert.equal(JSON.stringify(diseases).includes(directory), false);
  });

  test("requires authentication and returns a schema-checked, provenance-linked model score", async () => {
    const unauthenticated = await app.inject({
      method: "POST",
      url: "/api/v1/predictions",
      payload: { diseaseId: "brain_tumor", imagePath: "study.jpg" },
    });
    assert.equal(unauthenticated.statusCode, 401);

    const cookie = await register(app);
    const response = await app.inject({
      method: "POST",
      url: "/api/v1/predictions",
      headers: { cookie },
      payload: { diseaseId: "brain_tumor", imagePath: "study.jpg" },
    });
    assert.equal(response.statusCode, 200);
    const prediction = response.json().data.prediction;
    assert.equal(prediction.disease, "brain_tumor");
    assert.equal(prediction.class, "pituitary_tumor");
    assert.equal(prediction.model_score, 0.91);
    assert.equal(prediction.probability, undefined);
    assert.match(prediction.checkpoint_sha256, /^[a-f0-9]{64}$/);
    assert.equal(prediction.score_semantics, "Uncalibrated model output score; not a clinical probability.");
    assert.equal(prediction.research_only, true);
    assert.equal(prediction.diagnosis, false);
    const auditEvent = database.auditEvents.find((event) => event.event_type === "MODEL_INFERENCE_COMPLETED");
    assert.equal(auditEvent.metadata.model_id, "brain_tumour_cnn");
    assert.equal(JSON.stringify(auditEvent.metadata).includes("study.jpg"), false);
  });

  test("rejects paths outside the image root and unsupported tasks", async () => {
    const cookie = await register(app);
    const traversal = await app.inject({
      method: "POST",
      url: "/api/v1/predictions",
      headers: { cookie },
      payload: { diseaseId: "brain_tumor", imagePath: "../outside.jpg" },
    });
    assert.equal(traversal.statusCode, 400);
    assert.equal(traversal.json().error.code, "INVALID_IMAGE_PATH");

    const unsupported = await app.inject({
      method: "POST",
      url: "/api/v1/predictions",
      headers: { cookie },
      payload: { diseaseId: "diabetes", imagePath: "study.jpg" },
    });
    assert.equal(unsupported.statusCode, 503);
    assert.equal(unsupported.json().error.code, "MODEL_UNAVAILABLE");
  });

  test("rejects symlinks that resolve outside the configured image root", async () => {
    const cookie = await register(app);
    const outsideImage = path.join(directory, "outside.jpg");
    const linkedImage = path.join(config.predictionInputRoot, "linked.jpg");
    await writeFile(outsideImage, Buffer.from([0xff, 0xd8, 0xff, 0xd9]));
    await symlink(outsideImage, linkedImage);
    const response = await app.inject({
      method: "POST",
      url: "/api/v1/predictions",
      headers: { cookie },
      payload: { diseaseId: "brain_tumor", imagePath: "linked.jpg" },
    });
    assert.equal(response.statusCode, 400);
    assert.equal(response.json().error.code, "INVALID_IMAGE_PATH");
  });

  test("rejects image content whose signature does not match its extension", async () => {
    const cookie = await register(app);
    await writeFile(path.join(config.predictionInputRoot, "study.jpg"), "not a JPEG");
    const response = await app.inject({
      method: "POST",
      url: "/api/v1/predictions",
      headers: { cookie },
      payload: { diseaseId: "brain_tumor", imagePath: "study.jpg" },
    });
    assert.equal(response.statusCode, 422);
    assert.equal(response.json().error.code, "INVALID_IMAGE_CONTENT");
  });

  test("reports invalid model output rather than returning a success-shaped result", async () => {
    await writeRunner(config.brainTumorExecutable, { disease: "lung_cancer", class: "positive", probability: 2 });
    const cookie = await register(app);
    const response = await app.inject({
      method: "POST",
      url: "/api/v1/predictions",
      headers: { cookie },
      payload: { diseaseId: "brain_tumor", imagePath: "study.jpg" },
    });
    assert.equal(response.statusCode, 502);
    assert.equal(response.json().error.code, "MODEL_OUTPUT_INVALID");
    assert.ok(database.auditEvents.some((event) => event.event_type === "MODEL_INFERENCE_FAILED"));
  });

  test("reports missing model artifacts as unavailable", async () => {
    await rm(config.brainTumorModelPath);
    const response = await app.inject({ method: "GET", url: "/api/v1/predictions/catalog" });
    const brainTumor = response.json().data.diseases.find((disease) => disease.id === "brain_tumor");
    assert.equal(brainTumor.status, "unavailable");
    assert.equal(brainTumor.readiness, "assets_missing_or_unreadable");
    assert.equal(brainTumor.checkpoint_sha256, null);
  });

  test("labels repository sample inference as demo output and reports unavailable models", async () => {
    const cookie = await register(app);
    const response = await app.inject({
      method: "GET",
      url: "/api/v1/predictions/demo",
      headers: { cookie },
    });
    assert.equal(response.statusCode, 200);
    assert.equal(response.json().data.demo, true);
    const predictions = response.json().data.predictions;
    const brainTumor = predictions.find((prediction) => prediction.disease === "brain_tumor");
    const lung = predictions.find((prediction) => prediction.disease === "lung_cancer");
    assert.equal(brainTumor.status, "demo_prediction");
    assert.equal(brainTumor.model_score, 0.91);
    assert.equal(brainTumor.image_preview_available, true);
    assert.equal(lung.status, "model_unavailable");
    assert.equal(lung.diagnosis, false);
    assert.ok(database.auditEvents.some((event) => event.event_type === "MODEL_DEMO_EXECUTED"));
  });
});

async function writeRunner(executable, output) {
  const serialized = JSON.stringify(JSON.stringify(output));
  await writeFile(executable, `#!/usr/bin/env node\nprocess.stdout.write(${serialized});\n`);
  await chmod(executable, 0o755);
}
