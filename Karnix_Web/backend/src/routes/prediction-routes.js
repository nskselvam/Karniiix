import { createPredictionController } from "../controllers/prediction-controller.js";
import { catalogSchema, demoSchema, predictSchema } from "../schemas/prediction-schemas.js";

export async function predictionRoutes(fastify) {
  const controller = createPredictionController(fastify.services.predictionService);
  fastify.get("/catalog", { schema: catalogSchema }, controller.catalog);
  fastify.get("/demo", { schema: demoSchema, preHandler: [fastify.authenticate] }, controller.demo);
  fastify.post("/", { schema: predictSchema, preHandler: [fastify.authenticate] }, controller.predict);
}