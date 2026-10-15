import { standardErrors, successResponse, successUserResponse } from "./common.js";

const email = { type: "string", format: "email", minLength: 3, maxLength: 254 };
const password = { type: "string", minLength: 1, maxLength: 128 };

export const registerSchema = {
  tags: ["Authentication"],
  body: {
    type: "object",
    additionalProperties: false,
    required: ["name", "email", "password", "confirmPassword"],
    properties: {
      name: { type: "string", minLength: 2, maxLength: 120, pattern: "\\S" },
      email,
      password,
      confirmPassword: password,
    },
  },
  response: { 201: successUserResponse, ...standardErrors },
};

export const loginSchema = {
  tags: ["Authentication"],
  body: {
    type: "object",
    additionalProperties: false,
    required: ["password"],
    oneOf: [
      { required: ["email"], not: { required: ["identifier"] } },
      { required: ["identifier"], not: { required: ["email"] } },
    ],
    properties: {
      email,
      identifier: { type: "string", minLength: 3, maxLength: 254 },
      password,
    },
  },
  response: { 200: successUserResponse, ...standardErrors },
};

export const logoutSchema = {
  tags: ["Authentication"],
  security: [{ cookieAuth: [] }],
  response: { 200: successResponse, ...standardErrors },
};

export const meSchema = {
  tags: ["Authentication"],
  security: [{ cookieAuth: [] }],
  response: { 200: successUserResponse, ...standardErrors },
};
