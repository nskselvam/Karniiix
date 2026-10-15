import { errorResponse, standardErrors } from "./common.js";

const sourceSchema = {
  type: "object",
  additionalProperties: false,
  required: ["record_id", "recorded_at"],
  properties: {
    record_id: { type: "string", format: "uuid" },
    recorded_at: { type: "string", format: "date-time" },
  },
};

const responseSchema = {
  type: "object",
  additionalProperties: false,
  required: ["success", "data"],
  properties: {
    success: { const: true },
    data: {
      type: "object",
      additionalProperties: false,
      required: ["answer", "outcome", "sources"],
      properties: {
        answer: { type: "string" },
        outcome: {
          type: "string",
          enum: [
            "record_lookup",
            "missing",
            "conflict",
            "out_of_scope",
            "clinical_guidance",
            "emergency",
          ],
        },
        sources: { type: "array", items: sourceSchema },
      },
    },
  },
};

export const askPatientAssistantSchema = {
  tags: ["Patient record assistant"],
  security: [{ cookieAuth: [] }],
  body: {
    type: "object",
    additionalProperties: false,
    required: ["question"],
    properties: {
      question: { type: "string", minLength: 1, maxLength: 1000, pattern: "\\S" },
    },
  },
  response: { 200: responseSchema, 502: errorResponse, ...standardErrors },
};
