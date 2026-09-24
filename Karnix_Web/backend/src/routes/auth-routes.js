import { createAuthController } from "../controllers/auth-controller.js";
import { loginSchema, logoutSchema, meSchema, registerSchema } from "../schemas/auth-schemas.js";

export async function authRoutes(fastify) {
  const controller = createAuthController(fastify.services);
  const rateLimit = { max: fastify.config.authRateLimit, timeWindow: "1 minute" };

  fastify.post("/register", { schema: registerSchema, config: { rateLimit } }, controller.register);
  fastify.post("/login", { schema: loginSchema, config: { rateLimit } }, controller.login);
  fastify.post(
    "/logout",
    { schema: logoutSchema, preHandler: [fastify.authenticate] },
    controller.logout,
  );
  fastify.get("/me", { schema: meSchema, preHandler: [fastify.authenticate] }, controller.me);
}
