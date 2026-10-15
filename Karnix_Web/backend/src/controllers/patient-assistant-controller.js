export function createPatientAssistantController(service) {
  return {
    ask: async (request) => ({
      success: true,
      data: await service.ask(request.currentUser.id, request.body.question, request),
    }),
  };
}
