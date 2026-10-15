import { randomUUID } from "node:crypto";
import Fastify from "fastify";
import cookie from "@fastify/cookie";
import cors from "@fastify/cors";
import helmet from "@fastify/helmet";
import jwt from "@fastify/jwt";
import rateLimit from "@fastify/rate-limit";
import swagger from "@fastify/swagger";
import swaggerUi from "@fastify/swagger-ui";
import { createPool } from "./db/pool.js";
import { createAuthenticate } from "./middleware/authenticate.js";
import { createAuditRepository } from "./repositories/audit-repository.js";
import { authRoutes } from "./routes/auth-routes.js";
import { userRoutes } from "./routes/user-routes.js";
import { riskAssessmentRoutes } from "./routes/risk-assessment-routes.js";
import { createAccountService } from "./services/account-service.js";
import { createAuthService } from "./services/auth-service.js";
import { createPasswordService } from "./services/password-service.js";
import { createRiskAssessmentService } from "./services/risk-assessment-service.js";
import { createPredictionService } from "./services/prediction-service.js";
import { predictionRoutes } from "./routes/prediction-routes.js";
import { demoImageRoute } from "./routes/demo-image-route.js";
import { patientAssistantRoutes } from "./routes/patient-assistant-routes.js";
import { ACCESS_COOKIE, createSessionService } from "./services/session-service.js";
import { AppError } from "./utils/app-error.js";
import { createPatientAssistantService } from "./services/patient-assistant-service.js";

export async function buildApp({ config, database, logger = true, fetchImpl = globalThis.fetch }) {
  const pool = database ?? createPool(config);
  const app = Fastify({
    logger,
    bodyLimit: config.bodyLimit,
    trustProxy: config.trustProxy,
    ajv: { customOptions: { removeAdditional: false } },
    genReqId: (request) => request.headers["x-request-id"]?.slice(0, 128) || randomUUID(),
  });

  app.decorate("config", config);
  app.decorate("database", pool);
  const passwordService = createPasswordService(config.bcryptRounds);
  const sessionService = createSessionService(config);
  const authService = createAuthService({ pool, passwordService, sessionService });
  const accountService = createAccountService({ pool, passwordService });
  const riskAssessmentService = createRiskAssessmentService(pool);
  const predictionService = createPredictionService(config);
  const patientAssistantService = createPatientAssistantService({ database: pool, config, fetchImpl });
  const auditRepository = createAuditRepository(pool);
  app.decorate("sessionService", sessionService);
  app.decorate("services", { authService, accountService, sessionService, riskAssessmentService, predictionService, patientAssistantService, auditRepository });

  await app.register(helmet, { global: true });
  await app.register(cors, { origin: config.corsOrigin, credentials: true });
  await app.register(cookie);
  await app.register(jwt, {
    secret: config.jwtSecret,
    cookie: { cookieName: ACCESS_COOKIE, signed: false },
    sign: { iss: config.jwtIssuer, aud: config.jwtAudience },
    verify: { allowedIss: config.jwtIssuer, allowedAud: config.jwtAudience },
  });
  await app.register(rateLimit, { global: false });
  await app.register(swagger, {
    openapi: {
      info: { title: "Karnix Authentication API", version: "1.0.0" },
      components: { securitySchemes: { cookieAuth: { type: "apiKey", in: "cookie", name: ACCESS_COOKIE } } },
    },
  });
  await app.register(swaggerUi, { routePrefix: "/docs" });

  app.decorate("authenticate", createAuthenticate({ authService, sessionService, config }));

  app.addHook("onRequest", async (request) => {
    if (["POST", "PATCH", "PUT", "DELETE"].includes(request.method)) {
      const origin = request.headers.origin;
      if (origin && origin !== config.corsOrigin) throw new AppError(403, "INVALID_ORIGIN", "Request origin rejected");
    }
  });

  app.addHook("preValidation", async (request) => {
    if (request.body && typeof request.body === "object") {
      if (typeof request.body.email === "string") request.body.email = request.body.email.trim().toLowerCase();
      if (typeof request.body.name === "string") request.body.name = request.body.name.trim().replace(/\s+/g, " ");
    }
  });

  app.setErrorHandler((error, request, reply) => {
    if (error.validation) {
      return reply.code(422).send({
        success: false,
        error: { code: "VALIDATION_ERROR", message: "Request validation failed" },
        request_id: request.id,
      });
    }
    if (error instanceof AppError) {
      return reply.code(error.statusCode).send({
        success: false,
        error: { code: error.code, message: error.message },
        request_id: request.id,
      });
    }
    if (error.statusCode === 429) {
      return reply.code(429).send({
        success: false,
        error: { code: "RATE_LIMITED", message: "Too many requests" },
        request_id: request.id,
      });
    }
    request.log.error({ err: error }, "Unhandled request error");
    const unavailable = ["ECONNREFUSED", "57P01", "57P03"].includes(error.code);
    return reply.code(unavailable ? 503 : error.statusCode ?? 500).send({
      success: false,
      error: {
        code: unavailable ? "SERVICE_UNAVAILABLE" : "INTERNAL_ERROR",
        message: unavailable ? "Service temporarily unavailable" : "An unexpected error occurred",
      },
      request_id: request.id,
    });
  });

  app.get("/health", { schema: { hide: true } }, async () => ({ status: "ok" }));
  app.get("/ready", { schema: { hide: true } }, async (_request, reply) => {
    try {
      await pool.query("SELECT 1");
      return { status: "ready" };
    } catch {
      reply.code(503);
      return { status: "unavailable" };
    }
  });

  await app.register(authRoutes, { prefix: "/api/v1/auth" });
  await app.register(userRoutes, { prefix: "/api/v1/users" });
  await app.register(riskAssessmentRoutes, { prefix: "/api/v1/risk-assessments" });
  await app.register(predictionRoutes, { prefix: "/api/v1/predictions" });
  await app.register(demoImageRoute, { prefix: "/api/v1/predictions" });
  await app.register(patientAssistantRoutes, { prefix: "/api/v1/patient-assistant" });

  app.addHook("onClose", async () => {
    if (!database) await pool.end();
  });
  return app;
}
