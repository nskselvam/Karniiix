import { createRiskAssessmentController } from "../controllers/risk-assessment-controller.js";
import { assessmentByIdSchema, createAssessmentSchema, deleteAssessmentSchema, listAssessmentSchema, updateAssessmentSchema } from "../schemas/risk-assessment-schemas.js";

export async function riskAssessmentRoutes(fastify) {
  const controller = createRiskAssessmentController(fastify.services.riskAssessmentService);
  const auth = { preHandler: [fastify.authenticate] };
  fastify.post("/", { ...auth, schema: createAssessmentSchema }, controller.create);
  fastify.get("/", { ...auth, schema: listAssessmentSchema }, controller.list);
  fastify.get("/:id", { ...auth, schema: assessmentByIdSchema }, controller.find);
  fastify.patch("/:id", { ...auth, schema: updateAssessmentSchema }, controller.update);
  fastify.delete("/:id", { ...auth, schema: deleteAssessmentSchema }, controller.remove);
}
