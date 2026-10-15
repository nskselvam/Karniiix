import { createHash } from "node:crypto";
import { access, open, readFile, realpath, stat } from "node:fs/promises";
import path from "node:path";
import { constants as fileConstants } from "node:fs";
import { execFile } from "node:child_process";
import { promisify } from "node:util";
import { DISEASE_REGISTRY } from "../config/disease-registry.js";
import { AppError } from "../utils/app-error.js";

const execFileAsync = promisify(execFile);
const modelSettings = Object.freeze({
  lung_cancer: ["predictionExecutable", "predictionModelPath"],
  brain_tumor: ["brainTumorExecutable", "brainTumorModelPath"],
  osteoarthritis: ["osteoarthritisExecutable", "osteoarthritisModelPath"],
  lumbar_stenosis: ["lumbarStenosisExecutable", "lumbarStenosisModelPath"],
});
const scoreSemantics = "Uncalibrated model output score; not a clinical probability.";

function configuredPaths(config, diseaseId) {
  const settingNames = modelSettings[diseaseId];
  if (!settingNames) return null;
  return {
    executable: config[settingNames[0]],
    modelPath: config[settingNames[1]],
  };
}

function isWithinRoot(root, candidate) {
  const resolvedRoot = path.resolve(root);
  const resolvedCandidate = path.resolve(candidate);
  return resolvedCandidate === resolvedRoot || resolvedCandidate.startsWith(`${resolvedRoot}${path.sep}`);
}

async function imageSignatureMatches(imagePath, extension) {
  const handle = await open(imagePath, "r");
  try {
    const header = Buffer.alloc(8);
    const { bytesRead } = await handle.read(header, 0, header.length, 0);
    const bytes = header.subarray(0, bytesRead);
    if (extension === ".png") return bytes.equals(Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]));
    if (extension === ".jpg" || extension === ".jpeg") return bytes.length >= 3 && bytes[0] === 0xff && bytes[1] === 0xd8 && bytes[2] === 0xff;
    if (extension === ".bmp") return bytes.length >= 2 && bytes.toString("ascii", 0, 2) === "BM";
    if (extension === ".tif" || extension === ".tiff") {
      return bytes.length >= 4 && (
        bytes.toString("ascii", 0, 4) === "II*\u0000"
        || bytes.toString("ascii", 0, 4) === "MM\u0000*"
      );
    }
    if (extension === ".pgm" || extension === ".ppm") {
      return bytes.length >= 2 && ["P2", "P3", "P5", "P6"].includes(bytes.toString("ascii", 0, 2));
    }
    return false;
  } finally {
    await handle.close();
  }
}

function normalizeOutput(rawOutput, definition, checkpointSha256) {
  if (
    !rawOutput
    || typeof rawOutput !== "object"
    || Array.isArray(rawOutput)
    || rawOutput.disease !== definition.id
    || typeof rawOutput.class !== "string"
    || rawOutput.class.length === 0
    || !Number.isFinite(rawOutput.probability)
    || rawOutput.probability < 0
    || rawOutput.probability > 1
  ) {
    throw new AppError(502, "MODEL_OUTPUT_INVALID", "The model returned an invalid prediction result");
  }

  const classScores = rawOutput.probabilities ?? rawOutput.class_scores;
  if (
    classScores !== undefined
    && (
      !classScores
      || typeof classScores !== "object"
      || Array.isArray(classScores)
      || Object.values(classScores).some((score) => !Number.isFinite(score) || score < 0 || score > 1)
    )
  ) {
    throw new AppError(502, "MODEL_OUTPUT_INVALID", "The model returned invalid class scores");
  }

  return {
    disease: definition.id,
    model_id: definition.modelId,
    model_version: definition.version,
    checkpoint_sha256: checkpointSha256,
    modality: definition.modality,
    class: rawOutput.class,
    model_score: rawOutput.probability,
    score_semantics: scoreSemantics,
    ...(classScores === undefined ? {} : { class_scores: classScores }),
    research_only: true,
    diagnosis: false,
  };
}

export function createPredictionService(config) {
  async function inspectModel(diseaseId) {
    const definition = DISEASE_REGISTRY[diseaseId];
    const paths = configuredPaths(config, diseaseId);
    if (!definition || !definition.adapterAvailable || !paths || !paths.executable || !paths.modelPath) {
      return { status: "unavailable", readiness: "not_configured", checkpoint_sha256: null };
    }

    try {
      const [executableInfo, modelInfo] = await Promise.all([
        stat(paths.executable),
        stat(paths.modelPath),
        access(paths.executable, fileConstants.X_OK),
        access(paths.modelPath, fileConstants.R_OK),
      ]);
      if (!executableInfo.isFile() || !modelInfo.isFile()) {
        return { status: "unavailable", readiness: "invalid_assets", checkpoint_sha256: null };
      }
      const checkpoint = await readFile(paths.modelPath);
      return {
        status: "configured",
        readiness: "compatibility_not_verified",
        checkpoint_sha256: createHash("sha256").update(checkpoint).digest("hex"),
      };
    } catch {
      return { status: "unavailable", readiness: "assets_missing_or_unreadable", checkpoint_sha256: null };
    }
  }

  async function catalog() {
    return Promise.all(Object.values(DISEASE_REGISTRY).map(async (definition) => {
      const readiness = await inspectModel(definition.id);
      return {
        id: definition.id,
        model_id: definition.modelId,
        model_version: definition.version,
        label: definition.label,
        modality: definition.modality,
        engine: definition.engine,
        status: readiness.status,
        readiness: readiness.readiness,
        input: definition.input,
        input_formats: definition.inputFormats,
        preprocessing: definition.preprocessing,
        output_schema: definition.outputSchema,
        provenance: definition.provenance,
        evaluation: definition.evaluation,
        limitations: definition.limitations,
        resources: definition.resources,
        checkpoint_sha256: readiness.checkpoint_sha256,
        research_only: true,
        diagnosis: false,
      };
    }));
  }

  async function runPrediction(diseaseId, imagePath, enforceRoot) {
    const definition = DISEASE_REGISTRY[diseaseId];
    const paths = configuredPaths(config, diseaseId);
    if (!definition) throw new AppError(404, "DISEASE_NOT_SUPPORTED", "Disease model is not supported");
    if (!definition.adapterAvailable || !paths || !paths.executable || !paths.modelPath) {
      throw new AppError(503, "MODEL_UNAVAILABLE", "A research model is not configured for this task");
    }
    if (!imagePath) throw new AppError(400, "IMAGE_REQUIRED", "An image is required");
    if (enforceRoot && !config.predictionInputRoot) {
      throw new AppError(503, "IMAGE_STORAGE_UNAVAILABLE", "Research image storage is not configured");
    }

    let resolvedImage = enforceRoot
      ? path.resolve(config.predictionInputRoot, imagePath)
      : path.resolve(imagePath);
    if (enforceRoot) {
      if (!isWithinRoot(config.predictionInputRoot, resolvedImage)) {
        throw new AppError(400, "INVALID_IMAGE_PATH", "Image path is outside the configured input directory");
      }
      try {
        const [realRoot, realImage] = await Promise.all([
          realpath(config.predictionInputRoot),
          realpath(resolvedImage),
        ]);
        if (!isWithinRoot(realRoot, realImage)) {
          throw new AppError(400, "INVALID_IMAGE_PATH", "Image path is outside the configured input directory");
        }
        resolvedImage = realImage;
      } catch (error) {
        if (error instanceof AppError) throw error;
        throw new AppError(400, "IMAGE_UNAVAILABLE", "The requested image is unavailable");
      }
    }
    if (!definition.inputFormats.includes(path.extname(resolvedImage).toLowerCase())) {
      throw new AppError(415, "UNSUPPORTED_IMAGE_FORMAT", "The image format is not supported by this model adapter");
    }

    const readiness = await inspectModel(diseaseId);
    if (readiness.status !== "configured") {
      throw new AppError(503, "MODEL_UNAVAILABLE", "The configured model or checkpoint is unavailable");
    }
    try {
      const imageInfo = await stat(resolvedImage);
      if (!imageInfo.isFile()) throw new Error("Image is not a regular file");
    } catch {
      throw new AppError(400, "IMAGE_UNAVAILABLE", "The requested image is unavailable");
    }
    try {
      if (!await imageSignatureMatches(resolvedImage, path.extname(resolvedImage).toLowerCase())) {
        throw new AppError(422, "INVALID_IMAGE_CONTENT", "The image content does not match its file format");
      }
    } catch (error) {
      if (error instanceof AppError) throw error;
      throw new AppError(400, "IMAGE_UNAVAILABLE", "The requested image is unavailable");
    }

    let stdout;
    try {
      ({ stdout } = await execFileAsync(
        paths.executable,
        ["predict", "--model", paths.modelPath, "--image", resolvedImage],
        { timeout: 30_000, maxBuffer: 1_024 * 1_024, windowsHide: true },
      ));
    } catch {
      throw new AppError(502, "MODEL_EXECUTION_FAILED", "The research model could not complete prediction");
    }

    let rawOutput;
    try {
      rawOutput = JSON.parse(stdout);
    } catch {
      throw new AppError(502, "MODEL_OUTPUT_INVALID", "The model returned invalid output");
    }
    return normalizeOutput(rawOutput, definition, readiness.checkpoint_sha256);
  }

  return {
    catalog,
    predict: ({ diseaseId, imagePath }) => runPrediction(diseaseId, imagePath, true),
    async demo() {
      const demoImages = {
        lung_cancer: config.demoLungImage,
        brain_tumor: config.demoBrainTumorImage,
        osteoarthritis: config.demoOsteoarthritisImage,
        lumbar_stenosis: config.demoLumbarStenosisImage,
      };
      return Promise.all(Object.values(DISEASE_REGISTRY).map(async (definition) => {
        const readiness = await inspectModel(definition.id);
        if (readiness.status !== "configured") {
          return {
            disease: definition.id,
            status: "model_unavailable",
            message: "A research model is not configured for this task",
            research_only: true,
            diagnosis: false,
          };
        }
        const demoImage = demoImages[definition.id];
        if (!demoImage) {
          return {
            disease: definition.id,
            status: "demo_unavailable",
            message: "No repository demo image is configured for this task",
            research_only: true,
            diagnosis: false,
          };
        }
        try {
          const imagePreviewAvailable = [".jpg", ".jpeg", ".png"].includes(
            path.extname(demoImage).toLowerCase(),
          );
          return {
            ...(await runPrediction(definition.id, demoImage, false)),
            image_preview_available: imagePreviewAvailable,
            ...(imagePreviewAvailable
              ? { image_url: `/api/v1/predictions/demo-image/${definition.id}` }
              : {}),
            status: "demo_prediction",
          };
        } catch (error) {
          return {
            disease: definition.id,
            status: error instanceof AppError && error.code === "MODEL_UNAVAILABLE"
              ? "model_unavailable"
              : "demo_error",
            message: error instanceof AppError
              ? error.message
              : "The research demo could not complete",
            research_only: true,
            diagnosis: false,
          };
        }
      }));
    },
  };
}
