export const safeUserSchema = {
  type: "object",
  additionalProperties: false,
  required: ["id", "name", "email", "role"],
  properties: {
    id: { type: "string", format: "uuid" },
    name: { type: "string" },
    email: { type: "string", format: "email" },
    role: { type: "string", enum: ["user"] },
  },
};

export const successUserResponse = {
  type: "object",
  additionalProperties: false,
  required: ["success", "data"],
  properties: {
    success: { const: true },
    data: {
      type: "object",
      additionalProperties: false,
      required: ["user"],
      properties: { user: safeUserSchema },
    },
  },
};

export const successResponse = {
  type: "object",
  additionalProperties: false,
  required: ["success"],
  properties: { success: { const: true } },
};

export const errorResponse = {
  type: "object",
  additionalProperties: false,
  required: ["success", "error", "request_id"],
  properties: {
    success: { const: false },
    error: {
      type: "object",
      additionalProperties: false,
      required: ["code", "message"],
      properties: {
        code: { type: "string" },
        message: { type: "string" },
      },
    },
    request_id: { type: "string" },
  },
};

export const standardErrors = {
  400: errorResponse,
  401: errorResponse,
  403: errorResponse,
  409: errorResponse,
  422: errorResponse,
  429: errorResponse,
  500: errorResponse,
  503: errorResponse,
};
