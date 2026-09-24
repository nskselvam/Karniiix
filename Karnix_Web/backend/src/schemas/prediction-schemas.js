import { standardErrors } from "./common.js";

export const catalogSchema = {
  tags: ["Disease prediction"],
  response: { 200: { type: "object", required: ["success", "data"], properties: { success: { const: true }, data: { type: "object", required: ["diseases"], properties: { diseases: { type: "array" } } } } }, ...standardErrors },
};

export const demoSchema = {
  tags: ["Disease prediction"],
  security: [{ cookieAuth: [] }],
  response: { 200: { type: "object", required: ["success", "data"], properties: { success: { const: true }, data: { type: "object", required: ["predictions", "demo"], properties: { predictions: { type: "array" }, demo: { const: true } } } } }, ...standardErrors },
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
  response: { 200: { type: "object", required: ["success", "data"], properties: { success: { const: true }, data: { type: "object", required: ["prediction"], properties: { prediction: { type: "object" } } } } }, ...standardErrors },
};