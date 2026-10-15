import assert from "node:assert/strict";
import { test } from "node:test";
import { evaluateModels } from "../src/scripts/evaluate-medical-models.js";

test("evaluates multiple installed Ollama models against synthetic expected classifications", async () => {
  const fetchImpl = async (url, init = {}) => {
    if (url.endsWith("/api/tags")) {
      return Response.json({ models: [{ name: "good-medical-model" }, { name: "bad-medical-model" }] });
    }
    const request = JSON.parse(init.body);
    const question = JSON.parse(request.messages[1].content).question;
    const classifications = request.model === "good-medical-model"
      ? {
          "What is my smoking status?": { intent: "lookup", field_keys: ["smoking_status"] },
          "What are my smoking status and pack-years?": { intent: "lookup", field_keys: ["pack_years", "smoking_status"] },
          "What are my smoking status and age?": { intent: "lookup", field_keys: ["smoking_status", "age"] },
          "What is my tobacco type?": { intent: "lookup", field_keys: ["tobacco_type"] },
        }
      : {};
    return Response.json({
      message: {
        content: JSON.stringify(classifications[question] ?? { intent: "missing", field_keys: [] }),
      },
    });
  };

  const report = await evaluateModels({
    baseUrl: "http://127.0.0.1:11434",
    models: ["good-medical-model", "bad-medical-model"],
    fetchImpl,
  });

  assert.equal(report.case_count, 4);
  assert.equal(report.medical_accuracy_claim, false);
  assert.equal(report.protocol, "independent_classification_then_peer_review_then_unanimous_final");
  assert.equal(report.results[0].metrics.exact_match_accuracy, 1);
  assert.equal(report.results[0].metrics.schema_valid_rate, 1);
  assert.equal(report.results[1].metrics.exact_match_accuracy, 0);
  assert.equal(report.results[1].metrics.schema_valid_rate, 1);
  assert.equal(report.panel_consensus_rate, 0);
});

test("reports requested models that are not installed instead of substituting another model", async () => {
  const report = await evaluateModels({
    baseUrl: "http://127.0.0.1:11434",
    models: ["installed-model", "missing-model"],
    fetchImpl: async () => Response.json({ models: [{ name: "installed-model" }] }),
  });

  assert.equal(report.results[0].installed, true);
  assert.equal(report.results[1].installed, false);
  assert.equal(report.results[0].metrics.schema_valid_rate, 0);
  assert.equal(report.results[0].cases[0].error, "peer_model_not_installed");
  assert.equal(report.results[1].cases[0].error, "model_not_installed");
});
