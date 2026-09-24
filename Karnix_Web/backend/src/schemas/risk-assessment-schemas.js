import { standardErrors } from "./common.js";

const choices = (values) => ({ type: "string", enum: values });
const number = { type: "number" };
const bodyProperties = {
  age: { type: "integer", minimum: 18, maximum: 120 },
  gender: choices(["female", "male", "non_binary", "prefer_not_to_say"]),
  ethnicity: choices(["asian", "black", "hispanic_latino", "white", "middle_eastern", "indigenous", "other", "prefer_not_to_say"]),
  smoking_status: choices(["never", "former", "current"]),
  cigarettes_per_day: { ...number, minimum: 0, maximum: 200 },
  years_smoked: { ...number, minimum: 0, maximum: 105 },
  age_started_smoking: { type: ["integer", "null"], minimum: 5, maximum: 120 },
  secondhand_smoke_exposure: choices(["never", "occasional", "frequent"]),
  tobacco_type: choices(["cigarettes", "cigars", "pipe", "smokeless", "other", "none"]),
  other_tobacco_type: { type: ["string", "null"], maxLength: 120 },
  alcohol_consumption: choices(["never", "occasional", "moderate", "frequent"]),
  physical_activity: choices(["very_low", "low", "moderate", "high"]),
  weekly_physical_activity: choices(["none", "under_1_hour", "1_3_hours", "3_5_hours", "over_5_hours"]),
  sleep_duration: choices(["under_5_hours", "5_6_hours", "6_8_hours", "over_8_hours"]),
  diet_quality: choices(["poor", "average", "good"]),
  height_cm: { ...number, minimum: 80, maximum: 250 },
  weight_kg: { ...number, minimum: 20, maximum: 400 },
  air_pollution_exposure: choices(["low", "moderate", "high"]),
  living_environment: choices(["urban", "suburban", "rural"]),
  traffic_pollution_exposure: choices(["low", "moderate", "high"]),
  residential_pollution_exposure: choices(["low", "moderate", "high"]),
  occupational_exposure: choices(["yes", "no", "unknown"]),
  occupational_exposure_types: { type: "array", uniqueItems: true, maxItems: 7, items: choices(["radon", "asbestos", "diesel_exhaust", "chemical_dust", "industrial_fumes", "smoke", "other"]) },
  occupational_exposure_duration: { type: ["string", "null"], enum: ["under_1_year", "1_5_years", "5_10_years", "over_10_years", null] },
  family_history_lung_cancer: choices(["yes", "no", "unknown"]),
  family_history_other_cancer: choices(["yes", "no", "unknown"]),
  family_cancer_relationship: { type: ["string", "null"], enum: ["parent", "sibling", "child", "grandparent", "other_close_relative", "unknown", null] },
};

export const requiredFields = Object.keys(bodyProperties);
const assessmentObject = { type: "object", additionalProperties: false, required: requiredFields, properties: bodyProperties };
const assessmentResponse = { type: "object", additionalProperties: true };
const dataResponse = { type: "object", additionalProperties: false, required: ["success", "data"], properties: { success: { const: true }, data: { type: "object", additionalProperties: true } } };
const listResponse = { type: "object", additionalProperties: false, required: ["success", "data"], properties: { success: { const: true }, data: { type: "array", items: assessmentResponse } } };
const idParams = { type: "object", additionalProperties: false, required: ["id"], properties: { id: { type: "string", format: "uuid" } } };

export const createAssessmentSchema = { tags: ["Lung risk assessment"], security: [{ cookieAuth: [] }], body: assessmentObject, response: { 201: dataResponse, ...standardErrors } };
export const listAssessmentSchema = { tags: ["Lung risk assessment"], security: [{ cookieAuth: [] }], response: { 200: listResponse, ...standardErrors } };
export const assessmentByIdSchema = { tags: ["Lung risk assessment"], security: [{ cookieAuth: [] }], params: idParams, response: { 200: dataResponse, ...standardErrors } };
export const updateAssessmentSchema = { ...createAssessmentSchema, response: { 200: dataResponse, ...standardErrors } };
export const deleteAssessmentSchema = { tags: ["Lung risk assessment"], security: [{ cookieAuth: [] }], params: idParams, response: { 200: { type: "object", properties: { success: { const: true } } }, ...standardErrors } };
