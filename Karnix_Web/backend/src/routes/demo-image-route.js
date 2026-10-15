import { constants as fileConstants, createReadStream } from "node:fs";
import { access } from "node:fs/promises";
import path from "node:path";

export async function demoImageRoute(fastify) {
  const images = {
    lung_cancer: fastify.config.demoLungImage,
    brain_tumor: fastify.config.demoBrainTumorImage,
    osteoarthritis: fastify.config.demoOsteoarthritisImage,
    lumbar_stenosis: fastify.config.demoLumbarStenosisImage,
  };
  fastify.get("/demo-image/:diseaseId", { schema: { hide: true } }, async (request, reply) => {
    const imagePath = images[request.params.diseaseId];
    if (!imagePath) return reply.code(404).send({ error: "Demo image not found" });
    try {
      await access(imagePath, fileConstants.R_OK);
    } catch {
      return reply.code(404).send({ error: "Demo image not found" });
    }
    const extension = path.extname(imagePath).toLowerCase();
    const contentType = { ".jpg": "image/jpeg", ".jpeg": "image/jpeg", ".png": "image/png", ".ppm": "image/x-portable-pixmap", ".pgm": "image/x-portable-graymap" }[extension] ?? "application/octet-stream";
    return reply.type(contentType).send(createReadStream(imagePath));
  });
}