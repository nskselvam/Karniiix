import { createPatientAssistantController } from "../controllers/patient-assistant-controller.js";
import { askPatientAssistantSchema } from "../schemas/patient-assistant-schemas.js";

export async function patientAssistantRoutes(fastify) {
  const controller = createPatientAssistantController(
    fastify.services.patientAssistantService,
  );
  fastify.post(
    "/ask",
    {
      preHandler: [fastify.authenticate],
      config: { rateLimit: { max: 6, timeWindow: "1 minute" } },
      schema: askPatientAssistantSchema,
    },
    controller.ask,
  );
}
