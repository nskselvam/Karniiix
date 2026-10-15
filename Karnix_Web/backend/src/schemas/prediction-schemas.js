import { standardErrors } from "./common.js";

const modelScore = { type: "number", minimum: 0, maximum: 1 };
const classScores = {
  type: "object",
  additionalProperties: modelScore,
};
const predictionResult = {
  type: "object",
  additionalProperties: false,
  required: [
    "disease",
    "model_id",
    "model_version",
    "checkpoint_sha256",
    "modality",
    "class",
    "model_score",
    "score_semantics",
    "research_only",
    "diagnosis",
  ],
  properties: {
    disease: { type: "string" },
    model_id: { type: "string" },
    model_version: { type: "string" },
    checkpoint_sha256: { type: "string", pattern: "^[a-f0-9]{64}$" },
    modality: { type: "string" },
    class: { type: "string" },
    model_score: modelScore,
    score_semantics: { type: "string" },
    class_scores: { ...classScores, nullable: true },
    research_only: { const: true },
    diagnosis: { const: false },
  },
};
const demoResult = {
  type: "object",
  additionalProperties: false,
  required: ["disease", "status", "research_only", "diagnosis"],
  properties: {
    ...predictionResult.properties,
    status: { enum: ["demo_prediction", "demo_unavailable", "model_unavailable", "demo_error"] },
    message: { type: "string" },
    image_preview_available: { type: "boolean" },
    image_url: { type: "string" },
  },
};
const catalogItem = {
  type: "object",
  additionalProperties: false,
  required: [
    "id", "model_id", "model_version", "label", "modality", "engine", "status",
    "readiness", "input", "input_formats", "preprocessing", "output_schema",
    "provenance", "evaluation", "limitations", "resources", "checkpoint_sha256",
    "research_only", "diagnosis",
  ],
  properties: {
    id: { type: "string" },
    model_id: { type: "string" },
    model_version: { type: "string" },
    label: { type: "string" },
    modality: { type: "string" },
    engine: { type: "string", nullable: true },
    status: { enum: ["configured", "unavailable"] },
    readiness: { enum: ["compatibility_not_verified", "not_configured", "invalid_assets", "assets_missing_or_unreadable"] },
    input: { type: "string" },
    input_formats: { type: "array", items: { type: "string" } },
    preprocessing: { type: "string" },
    output_schema: { type: "string" },
    provenance: { type: "string" },
    evaluation: { type: "string" },
    limitations: { type: "string" },
    resources: { type: "string" },
    checkpoint_sha256: { type: "string", nullable: true, pattern: "^[a-f0-9]{64}$" },
    research_only: { const: true },
    diagnosis: { const: false },
  },
};

export const catalogSchema = {
  tags: ["Disease prediction"],
  response: { 200: { type: "object", required: ["success", "data"], properties: { success: { const: true }, data: { type: "object", required: ["diseases"], properties: { diseases: { type: "array", items: catalogItem } } } } }, ...standardErrors },
};

export const demoSchema = {
  tags: ["Disease prediction"],
  security: [{ cookieAuth: [] }],
  response: { 200: { type: "object", required: ["success", "data"], properties: { success: { const: true }, data: { type: "object", required: ["predictions", "demo"], properties: { predictions: { type: "array", items: demoResult }, demo: { const: true } } } } }, ...standardErrors },
};

export const predictSchema = {
  tags: ["Disease prediction"],
  security: [{ cookieAuth: [] }],
  body: {
    type: "object",
    additionalProperties: false,
    required: ["diseaseId", "imagePath"],
    properties: {
      diseaseId: { type: "string", minLength: 2, maxLength: 64 },
      imagePath: { type: "string", minLength: 1, maxLength: 512 },
    },
  },
  response: { 200: { type: "object", required: ["success", "data"], properties: { success: { const: true }, data: { type: "object", required: ["prediction"], properties: { prediction: predictionResult } } } }, ...standardErrors },
};