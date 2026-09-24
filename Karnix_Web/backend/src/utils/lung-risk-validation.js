import { badRequest } from "./app-error.js";

export const enums = {
  gender: ["female", "male", "non_binary", "prefer_not_to_say"],
  ethnicity: ["asian", "black", "hispanic_latino", "white", "middle_eastern", "indigenous", "other", "prefer_not_to_say"],
  smoking_status: ["never", "former", "current"],
  secondhand_smoke_exposure: ["never", "occasional", "frequent"],
  alcohol_consumption: ["never", "occasional", "moderate", "frequent"],
  tobacco_type: ["cigarettes", "cigars", "pipe", "smokeless", "other", "none"],
  physical_activity: ["very_low", "low", "moderate", "high"],
  weekly_physical_activity: ["none", "under_1_hour", "1_3_hours", "3_5_hours", "over_5_hours"],
  sleep_duration: ["under_5_hours", "5_6_hours", "6_8_hours", "over_8_hours"],
  diet_quality: ["poor", "average", "good"],
  exposure_level: ["low", "moderate", "high"],
  living_environment: ["urban", "suburban", "rural"],
  occupational_exposure: ["yes", "no", "unknown"],
  occupational_exposure_duration: ["under_1_year", "1_5_years", "5_10_years", "over_10_years"],
  family_history: ["yes", "no", "unknown"],
  family_cancer_relationship: ["parent", "sibling", "child", "grandparent", "other_close_relative", "unknown"],
};

export const exposureTypes = ["radon", "asbestos", "diesel_exhaust", "chemical_dust", "industrial_fumes", "smoke", "other"];

const required = (value, field) => {
  if (value === undefined || value === null || value === "") throw badRequest("VALIDATION_ERROR", `${field} is required`);
};
const choice = (value, field) => {
  if (!enums[field].includes(value)) throw badRequest("VALIDATION_ERROR", `Invalid ${field}`);
};
const nonNegative = (value, field) => {
  if (typeof value !== "number" || !Number.isFinite(value) || value < 0) throw badRequest("VALIDATION_ERROR", `Invalid ${field}`);
};

export function normalizeAndCalculate(input) {
  const data = { ...input };
  for (const field of ["age", "gender", "ethnicity", "smoking_status", "secondhand_smoke_exposure", "alcohol_consumption", "tobacco_type", "physical_activity", "weekly_physical_activity", "sleep_duration", "diet_quality", "height_cm", "weight_kg", "air_pollution_exposure", "living_environment", "traffic_pollution_exposure", "residential_pollution_exposure", "occupational_exposure", "family_history_lung_cancer", "family_history_other_cancer"]) required(data[field], field);
  const submittedEnumFields = [
    "gender", "ethnicity", "smoking_status", "secondhand_smoke_exposure", "alcohol_consumption",
    "tobacco_type", "physical_activity", "weekly_physical_activity", "sleep_duration", "diet_quality",
    "air_pollution_exposure", "living_environment", "traffic_pollution_exposure", "residential_pollution_exposure",
    "occupational_exposure", "family_history_lung_cancer", "family_history_other_cancer",
  ];
  for (const field of submittedEnumFields) {
    const enumField = ["air_pollution_exposure", "traffic_pollution_exposure", "residential_pollution_exposure"].includes(field)
      ? "exposure_level"
      : ["family_history_lung_cancer", "family_history_other_cancer"].includes(field)
        ? "family_history"
        : field;
    choice(data[field], enumField);
  }
  if (data.age < 18 || data.age > 120) throw badRequest("VALIDATION_ERROR", "Age must be between 18 and 120");
  for (const field of ["cigarettes_per_day", "years_smoked", "height_cm", "weight_kg"]) nonNegative(data[field] ?? 0, field);
  if (data.height_cm < 80 || data.height_cm > 250 || data.weight_kg < 20 || data.weight_kg > 400) throw badRequest("VALIDATION_ERROR", "Height or weight is outside the allowed range");

  if (data.smoking_status === "never") {
    data.cigarettes_per_day = 0;
    data.years_smoked = 0;
    data.age_started_smoking = null;
    if (data.tobacco_type !== "none") throw badRequest("VALIDATION_ERROR", "Never smokers must select no tobacco use");
  } else {
    required(data.age_started_smoking, "age_started_smoking");
    if (data.age_started_smoking < 5 || data.age_started_smoking >= data.age) throw badRequest("VALIDATION_ERROR", "Age started smoking must be less than current age");
    if (data.cigarettes_per_day <= 0 || data.years_smoked <= 0) throw badRequest("VALIDATION_ERROR", "Smoking history must be greater than zero");
  }
  if (data.tobacco_type === "other") required(data.other_tobacco_type, "other_tobacco_type");
  else data.other_tobacco_type = null;
  data.pack_years = data.smoking_status === "never" ? 0 : Number(((data.cigarettes_per_day * data.years_smoked) / 20).toFixed(2));

  const heightMeters = data.height_cm / 100;
  data.bmi = Number((data.weight_kg / (heightMeters * heightMeters)).toFixed(2));

  const types = data.occupational_exposure_types ?? [];
  if (!Array.isArray(types) || types.some((type) => !exposureTypes.includes(type)) || new Set(types).size !== types.length) throw badRequest("VALIDATION_ERROR", "Invalid occupational exposure types");
  if (data.occupational_exposure === "yes") {
    if (!types.length || !enums.occupational_exposure_duration.includes(data.occupational_exposure_duration)) throw badRequest("VALIDATION_ERROR", "Occupational exposure details are required");
  } else if (types.length || data.occupational_exposure_duration) {
    throw badRequest("VALIDATION_ERROR", "Occupational details require a yes answer");
  }
  if (data.family_history_lung_cancer !== "yes") data.family_cancer_relationship = null;
  else {
    required(data.family_cancer_relationship, "family_cancer_relationship");
    choice(data.family_cancer_relationship, "family_cancer_relationship");
  }
  return data;
}
