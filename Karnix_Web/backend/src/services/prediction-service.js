import { access } from "node:fs/promises";
import path from "node:path";
import { execFile } from "node:child_process";
import { promisify } from "node:util";
import { DISEASE_REGISTRY, publicDiseaseCatalog } from "../config/disease-registry.js";
import { AppError } from "../utils/app-error.js";

const execFileAsync = promisify(execFile);

export function createPredictionService(config) {
  const modelConfigFor = (diseaseId) => diseaseId === "osteoarthritis"
    ? { executable: config.osteoarthritisExecutable, modelPath: config.osteoarthritisModelPath }
    : diseaseId === "brain_tumor"
      ? { executable: config.brainTumorExecutable, modelPath: config.brainTumorModelPath }
      : diseaseId === "lumbar_stenosis"
        ? { executable: config.lumbarStenosisExecutable, modelPath: config.lumbarStenosisModelPath }
        : { executable: config.predictionExecutable, modelPath: config.predictionModelPath };

  async function runPrediction(diseaseId, imagePath, enforceRoot) {
    const definition = DISEASE_REGISTRY[diseaseId];
    if (!definition) throw new AppError(404, "DISEASE_NOT_SUPPORTED", "Disease model is not supported");
    const modelConfig = modelConfigFor(diseaseId);
    if (definition.status !== "adapter_ready" || !modelConfig.executable || !modelConfig.modelPath) {
      throw new AppError(503, "MODEL_UNAVAILABLE", "A validated model is not available for this disease");
    }
    if (!imagePath) throw new AppError(400, "IMAGE_REQUIRED", "A server-local image is required");
    const resolvedImage = enforceRoot
      ? path.resolve(config.predictionInputRoot, imagePath)
      : path.resolve(imagePath);
    if (enforceRoot && (!config.predictionInputRoot || (resolvedImage !== path.resolve(config.predictionInputRoot) && !resolvedImage.startsWith(`${path.resolve(config.predictionInputRoot)}${path.sep}`)))) {
      throw new AppError(400, "INVALID_IMAGE_PATH", "Image path is outside the configured input directory");
    }
    try {
      await access(resolvedImage);
      await access(modelConfig.executable);
      await access(modelConfig.modelPath);
    } catch {
      throw new AppError(503, "MODEL_UNAVAILABLE", "The configured model or image is unavailable");
    }
    try {
      const { stdout } = await execFileAsync(modelConfig.executable, ["predict", "--model", modelConfig.modelPath, "--image", resolvedImage], { timeout: 30_000, maxBuffer: 1_024 * 1024 });
      const output = JSON.parse(stdout);
      return { ...output, disease: diseaseId, research_only: true, diagnosis: false };
    } catch {
      throw new AppError(502, "MODEL_EXECUTION_FAILED", "The research model could not complete prediction");
    }
  }

  return {
    catalog: publicDiseaseCatalog,
    predict: ({ diseaseId, imagePath }) => runPrediction(diseaseId, imagePath, true),
    async demo() {
      const demos = {
        lung_cancer: config.demoLungImage,
        brain_tumor: config.demoBrainTumorImage,
        osteoarthritis: config.demoOsteoarthritisImage,
        lumbar_stenosis: config.demoLumbarStenosisImage,
      };
      return Promise.all(Object.keys(DISEASE_REGISTRY).map(async (diseaseId) => {
        if (!demos[diseaseId] || DISEASE_REGISTRY[diseaseId].status !== "adapter_ready") return { disease: diseaseId, status: "model_unavailable", research_only: true, diagnosis: false };
        try { return { ...(await runPrediction(diseaseId, demos[diseaseId], false)), image_url: `/api/v1/predictions/demo-image/${diseaseId}`, status: "demo_prediction" }; }
        catch (error) { return { disease: diseaseId, status: "demo_error", message: error.message, research_only: true, diagnosis: false }; }
      }));
    },
  };
}