import { standardErrors, successResponse, successUserResponse } from "./common.js";

export const getAccountSchema = {
  tags: ["User account"],
  security: [{ cookieAuth: [] }],
  response: { 200: successUserResponse, ...standardErrors },
};

export const updateAccountSchema = {
  tags: ["User account"],
  security: [{ cookieAuth: [] }],
  body: {
    type: "object",
    additionalProperties: false,
    required: ["name"],
    properties: { name: { type: "string", minLength: 2, maxLength: 120, pattern: "\\S" } },
  },
  response: { 200: successUserResponse, ...standardErrors },
};

export const changePasswordSchema = {
  tags: ["User account"],
  security: [{ cookieAuth: [] }],
  body: {
    type: "object",
    additionalProperties: false,
    required: ["currentPassword", "newPassword", "confirmNewPassword"],
    properties: {
      currentPassword: { type: "string", minLength: 1, maxLength: 128 },
      newPassword: { type: "string", minLength: 1, maxLength: 128 },
      confirmNewPassword: { type: "string", minLength: 1, maxLength: 128 },
    },
  },
  response: { 200: successResponse, ...standardErrors },
};

export const deactivateAccountSchema = {
  tags: ["User account"],
  security: [{ cookieAuth: [] }],
  response: { 200: successResponse, ...standardErrors },
};
