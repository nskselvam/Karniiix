export function createPredictionController(service, audit) {
  async function recordEvent(request, eventType, metadata) {
    await audit.record({
      userId: request.currentUser.id,
      eventType,
      requestId: request.id,
      ipAddress: request.ip,
      userAgent: request.headers["user-agent"],
      metadata,
    });
  }

  return {
    catalog: async () => ({ success: true, data: { diseases: await service.catalog() } }),
    predict: async (request) => {
      let prediction;
      try {
        prediction = await service.predict(request.body);
      } catch (error) {
        await recordEvent(request, "MODEL_INFERENCE_FAILED", {
          disease_id: request.body.diseaseId,
          error_code: error.code ?? "MODEL_EXECUTION_FAILED",
          mode: "server_local_image",
        });
        throw error;
      }
      await recordEvent(request, "MODEL_INFERENCE_COMPLETED", {
        disease_id: prediction.disease,
        model_id: prediction.model_id,
        checkpoint_sha256: prediction.checkpoint_sha256,
        mode: "server_local_image",
      });
      return { success: true, data: { prediction } };
    },
    demo: async (request) => {
      const predictions = await service.demo();
      await recordEvent(request, "MODEL_DEMO_EXECUTED", {
        tasks: predictions.map(({ disease, status, model_id, checkpoint_sha256 }) => ({
          disease_id: disease,
          status,
          model_id,
          checkpoint_sha256,
        })),
      });
      return { success: true, data: { predictions, demo: true } };
    },
  };
}