export function createPredictionController(service) {
  return {
    catalog: async () => ({ success: true, data: { diseases: service.catalog() } }),
    predict: async (request) => ({ success: true, data: { prediction: await service.predict(request.body) } }),
    demo: async () => ({ success: true, data: { predictions: await service.demo(), demo: true } }),
  };
}