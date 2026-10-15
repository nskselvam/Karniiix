import { pathToFileURL } from "node:url";
import { localOllamaUrl } from "../config/env.js";
import { classifyWithOllama } from "../services/patient-assistant-service.js";

const evaluationCases = [
  {
    id: "smoking-status-lookup",
    question: "What is my smoking status?",
    availableFields: ["smoking_status"],
    rows: [
      {
        id: "synthetic-record-1",
        created_at: "2025-01-02T03:04:05.000Z",
        smoking_status: "former",
      },
    ],
    expected: { intent: "lookup", field_keys: ["smoking_status"] },
  },
  {
    id: "two-field-lookup",
    question: "What are my smoking status and pack-years?",
    availableFields: ["smoking_status", "pack_years"],
    rows: [
      {
        id: "synthetic-record-2",
        created_at: "2025-01-03T03:04:05.000Z",
        smoking_status: "former",
        pack_years: 12.5,
      },
    ],
    expected: { intent: "lookup", field_keys: ["smoking_status", "pack_years"] },
  },
  {
    id: "smoking-and-age-lookup",
    question: "What are my smoking status and age?",
    availableFields: ["smoking_status", "age"],
    rows: [
      {
        id: "synthetic-record-3",
        created_at: "2025-01-04T03:04:05.000Z",
        smoking_status: "former",
        age: 52,
      },
    ],
    expected: { intent: "lookup", field_keys: ["smoking_status", "age"] },
  },
  {
    id: "tobacco-type-lookup",
    question: "What is my tobacco type?",
    availableFields: ["tobacco_type"],
    rows: [
      {
        id: "synthetic-record-4",
        created_at: "2025-01-05T03:04:05.000Z",
        tobacco_type: ["cigarette"],
      },
    ],
    expected: { intent: "lookup", field_keys: ["tobacco_type"] },
  },
];

function classificationMatches(actual, expected) {
  return actual.intent === expected.intent &&
    actual.field_keys.length === expected.field_keys.length &&
    [...actual.field_keys].sort().every(
      (key, index) => key === [...expected.field_keys].sort()[index],
    );
}

function classificationKey(classification) {
  return JSON.stringify({
    intent: classification.intent,
    field_keys: [...classification.field_keys].sort(),
  });
}

function percentile(values, fraction) {
  if (values.length === 0) return null;
  const sorted = [...values].sort((left, right) => left - right);
  return sorted[Math.ceil(fraction * sorted.length) - 1];
}

async function getInstalledModels(baseUrl, fetchImpl) {
  const controller = new AbortController();
  const timer = setTimeout(() => controller.abort(), 5_000);
  try {
    const response = await fetchImpl(`${baseUrl}/api/tags`, {
      method: "GET",
      signal: controller.signal,
    });
    if (!response.ok) {
      throw new Error(`Ollama model-list request failed with HTTP ${response.status}.`);
    }
    const payload = await response.json();
    if (!Array.isArray(payload?.models) ||
        payload.models.some((model) => typeof model?.name !== "string")) {
      throw new Error("Ollama returned an invalid model list.");
    }
    return payload.models.map((model) => model.name);
  } finally {
    clearTimeout(timer);
  }
}

export async function evaluateModels({
  baseUrl,
  models,
  fetchImpl = globalThis.fetch,
}) {
  if (!Array.isArray(models) || models.length < 2 ||
      models.some((model) => typeof model !== "string" || !model.trim()) ||
      new Set(models).size !== models.length) {
    throw new Error("Provide at least two distinct, non-empty Ollama model names.");
  }
  const installedModels = await getInstalledModels(baseUrl, fetchImpl);
  const allInstalled = models.every((model) => installedModels.includes(model));
  const byModel = new Map(models.map((model) => [
    model,
    { cases: [], latencies: [], initialCorrect: 0, correct: 0, schemaValid: 0 },
  ]));
  let unanimousPanels = 0;

  for (const evaluationCase of evaluationCases) {
    if (!allInstalled) {
      for (const model of models) {
        byModel.get(model).cases.push({
          id: evaluationCase.id,
          valid: false,
          initial_passed: false,
          passed: false,
          panel_consensus: false,
          latency_ms: null,
          error: installedModels.includes(model)
            ? "peer_model_not_installed"
            : "model_not_installed",
        });
      }
      continue;
    }

    const observed = new Map(models.map((model) => [
      model,
      { initial: null, final: null, latency: 0 },
    ]));
    let inferenceError = null;
    try {
      await classifyWithOllama({
        config: { ollamaBaseUrl: baseUrl, ollamaModels: models },
        fetchImpl,
        question: evaluationCase.question,
        rows: evaluationCase.rows,
        availableFields: evaluationCase.availableFields,
        onClassification({ model, round, classification, latencyMs }) {
          const result = observed.get(model);
          result[round === "independent_classification" ? "initial" : "final"] =
            classification;
          result.latency += latencyMs;
        },
      });
    } catch (error) {
      inferenceError = error.code ?? "inference_error";
    }

    const finalResults = models
      .map((model) => observed.get(model).final)
      .filter(Boolean);
    const panelConsensus = finalResults.length === models.length &&
      finalResults.every((result) => classificationKey(result) === classificationKey(finalResults[0]));
    if (panelConsensus) unanimousPanels += 1;

    for (const model of models) {
      const observation = observed.get(model);
      const valid = observation.final !== null;
      const initialPassed = observation.initial !== null &&
        classificationMatches(observation.initial, evaluationCase.expected);
      const passed = valid &&
        classificationMatches(observation.final, evaluationCase.expected);
      const result = byModel.get(model);
      if (initialPassed) result.initialCorrect += 1;
      if (valid) result.schemaValid += 1;
      if (passed) result.correct += 1;
      if (observation.latency > 0) result.latencies.push(observation.latency);
      result.cases.push({
        id: evaluationCase.id,
        valid,
        initial_passed: initialPassed,
        passed,
        panel_consensus: panelConsensus,
        latency_ms: observation.latency || null,
        ...(!valid ? { error: inferenceError ?? "inference_error" } : {}),
      });
    }
  }

  return {
    evaluation: "synthetic_patient_assistant_classification",
    protocol: "independent_classification_then_peer_review_then_unanimous_final",
    medical_accuracy_claim: false,
    case_count: evaluationCases.length,
    panel_consensus_rate: unanimousPanels / evaluationCases.length,
    results: models.map((model) => {
      const result = byModel.get(model);
      return {
        model,
        installed: installedModels.includes(model),
        metrics: {
          initial_exact_match_accuracy: result.initialCorrect / evaluationCases.length,
          exact_match_accuracy: result.correct / evaluationCases.length,
          schema_valid_rate: result.schemaValid / evaluationCases.length,
          average_latency_ms: result.latencies.length
            ? Math.round(result.latencies.reduce((sum, latency) => sum + latency, 0) / result.latencies.length)
            : null,
          p95_latency_ms: percentile(result.latencies, 0.95),
        },
        cases: result.cases,
      };
    }),
  };
}

function parseArguments(args) {
  const options = { models: [] };
  for (let index = 0; index < args.length; index += 1) {
    const argument = args[index];
    if (argument === "--models") {
      const value = args[++index];
      if (!value) throw new Error("--models requires a comma-separated list of installed model names.");
      options.models.push(...value.split(",").map((model) => model.trim()).filter(Boolean));
    } else if (argument === "--help" || argument === "-h") {
      options.help = true;
    } else {
      throw new Error(`Unknown option: ${argument}`);
    }
  }
  if (options.models.length === 0 && process.env.KARNIX_MODELS?.trim()) {
    options.models.push(...process.env.KARNIX_MODELS.split(",").map((model) => model.trim()).filter(Boolean));
  } else if (options.models.length === 0 && process.env.KARNIX_MODEL?.trim()) {
    options.models.push(process.env.KARNIX_MODEL.trim());
  }
  return options;
}

export async function main(args = process.argv.slice(2)) {
  const options = parseArguments(args);
  if (options.help) {
    console.log("Usage: npm run evaluate:medical-models -- [--models name1,name2]");
    console.log("Uses KARNIX_MODELS when --models is omitted. At least two models must already be installed in local Ollama.");
    return;
  }
  if (options.models.length === 0) {
    throw new Error("Set KARNIX_MODELS or provide --models name1,name2.");
  }

  const baseUrl = localOllamaUrl(process.env.OLLAMA_BASE_URL ?? "http://127.0.0.1:11434");
  const report = await evaluateModels({ baseUrl, models: options.models });
  console.log(JSON.stringify(report, null, 2));
  if (report.results.some((result) =>
    !result.installed || result.metrics.schema_valid_rate < 1
  )) {
    process.exitCode = 1;
  }
}

if (process.argv[1] && import.meta.url === pathToFileURL(process.argv[1]).href) {
  main().catch((error) => {
    console.error(`Medical model evaluation failed: ${error.message}`);
    process.exitCode = 1;
  });
}
