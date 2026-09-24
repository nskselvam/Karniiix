export function createRiskAssessmentController(service) {
  return {
    create: async (request, reply) => {
      const assessment = await service.create(request.currentUser.id, request.body, request);
      reply.code(201);
      return { success: true, data: { assessment } };
    },
    list: async (request) => ({ success: true, data: await service.list(request.currentUser.id) }),
    find: async (request, reply) => {
      const assessment = await service.find(request.currentUser.id, request.params.id);
      if (!assessment) return reply.code(404).send({ success: false, error: { code: "NOT_FOUND", message: "Assessment not found" }, request_id: request.id });
      return { success: true, data: { assessment } };
    },
    update: async (request, reply) => {
      const assessment = await service.update(request.currentUser.id, request.params.id, request.body, request);
      if (!assessment) return reply.code(404).send({ success: false, error: { code: "NOT_FOUND", message: "Assessment not found" }, request_id: request.id });
      return { success: true, data: { assessment } };
    },
    remove: async (request, reply) => {
      const removed = await service.remove(request.currentUser.id, request.params.id, request);
      if (!removed) return reply.code(404).send({ success: false, error: { code: "NOT_FOUND", message: "Assessment not found" }, request_id: request.id });
      return { success: true };
    },
  };
}
