import assert from "node:assert/strict";
import { test } from "node:test";
import { normalizeAndCalculate } from "../src/utils/lung-risk-validation.js";

const base = {
  age: 52, gender: "male", ethnicity: "white", smoking_status: "former", cigarettes_per_day: 10, years_smoked: 20,
  age_started_smoking: 25, secondhand_smoke_exposure: "occasional", tobacco_type: "cigarettes", other_tobacco_type: null,
  alcohol_consumption: "occasional", physical_activity: "moderate", weekly_physical_activity: "1_3_hours", sleep_duration: "6_8_hours", diet_quality: "average",
  height_cm: 180, weight_kg: 88, air_pollution_exposure: "high", living_environment: "urban", traffic_pollution_exposure: "high", residential_pollution_exposure: "moderate",
  occupational_exposure: "yes", occupational_exposure_types: ["diesel_exhaust", "chemical_dust"], occupational_exposure_duration: "5_10_years",
  family_history_lung_cancer: "no", family_history_other_cancer: "yes", family_cancer_relationship: null,
};

test("calculates pack-years and BMI on the backend", () => {
  const result = normalizeAndCalculate(base);
  assert.equal(result.pack_years, 10);
  assert.equal(result.bmi, 27.16);
});

test("normalizes never smokers to zero history", () => {
  const result = normalizeAndCalculate({ ...base, smoking_status: "never", cigarettes_per_day: 40, years_smoked: 20, age_started_smoking: 25, tobacco_type: "none", occupational_exposure: "no", occupational_exposure_types: [], occupational_exposure_duration: null });
  assert.equal(result.pack_years, 0);
  assert.equal(result.cigarettes_per_day, 0);
  assert.equal(result.age_started_smoking, null);
});

test("rejects impossible smoking and occupational combinations", () => {
  assert.throws(() => normalizeAndCalculate({ ...base, age_started_smoking: 60 }), /Age started smoking/);
  assert.throws(() => normalizeAndCalculate({ ...base, occupational_exposure: "no" }), /Occupational details/);
  assert.throws(() => normalizeAndCalculate({ ...base, weight_kg: -2 }), /Invalid weight_kg/);
});