export const DISEASE_REGISTRY = Object.freeze({
  lung_cancer: {
    id: "lung_cancer",
    label: "Lung cancer risk research model",
    modality: "xray",
    engine: "Other_Disease/lung_xray_ml",
    status: "adapter_ready",
    input: "Server-local lung X-ray image",
  },
  brain_tumor: {
    id: "brain_tumor",
    label: "Brain tumor MRI classifier",
    modality: "mri",
    engine: "Other_Disease/brain_tumor_ml",
    status: "adapter_ready",
    input: "Server-local brain MRI image",
  },
  osteoarthritis: {
    id: "osteoarthritis",
    label: "Osteoarthritis X-ray classifier",
    modality: "xray",
    engine: "Other_Disease/osteoarthritis_ml",
    status: "adapter_ready",
    input: "Server-local knee X-ray image",
  },
  lumbar_stenosis: {
    id: "lumbar_stenosis",
    label: "Lumbar stenosis MRI classifier",
    modality: "mri",
    engine: "Other_Disease/lumbar_stenosis_ml",
    status: "adapter_ready",
    input: "Server-local lumbar MRI image",
  },
  diabetes: {
    id: "diabetes",
    label: "Diabetes risk model",
    modality: "structured_data",
    engine: null,
    status: "model_unavailable",
    input: "Disease-specific validated feature contract required",
  },
  heart_disease: {
    id: "heart_disease",
    label: "Heart disease risk model",
    modality: "structured_data",
    engine: null,
    status: "model_unavailable",
    input: "Disease-specific validated feature contract required",
  },
});

export function publicDiseaseCatalog() {
  return Object.values(DISEASE_REGISTRY).map(({ id, label, modality, status, input }) => ({ id, label, modality, status, input, research_only: true, diagnosis: false }));
}
