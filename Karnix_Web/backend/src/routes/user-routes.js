import { createUserController } from "../controllers/user-controller.js";
import {
  changePasswordSchema,
  deactivateAccountSchema,
  getAccountSchema,
  updateAccountSchema,
} from "../schemas/user-schemas.js";

export async function userRoutes(fastify) {
  const controller = createUserController(fastify.services);
  const authenticated = { preHandler: [fastify.authenticate] };

  fastify.get("/me", { ...authenticated, schema: getAccountSchema }, controller.get);
  fastify.patch("/me", { ...authenticated, schema: updateAccountSchema }, controller.update);
  fastify.delete("/me", { ...authenticated, schema: deactivateAccountSchema }, controller.deactivate);
  fastify.post(
    "/me/change-password",
    {
      ...authenticated,
      schema: changePasswordSchema,
      config: { rateLimit: { max: fastify.config.authRateLimit, timeWindow: "1 minute" } },
    },
    controller.changePassword,
  );
}
