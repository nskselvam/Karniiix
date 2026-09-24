exports.up = (pgm) => {
  pgm.addColumn("audit_events", {
    resource_id: { type: "uuid" },
  });

  pgm.createTable("lung_risk_assessments", {
    id: { type: "uuid", primaryKey: true, default: pgm.func("gen_random_uuid()") },
    user_id: { type: "uuid", notNull: true, references: "users", onDelete: "CASCADE" },
    age: { type: "smallint", notNull: true },
    gender: { type: "varchar(32)", notNull: true },
    ethnicity: { type: "varchar(64)", notNull: true },
    smoking_status: { type: "varchar(16)", notNull: true },
    cigarettes_per_day: { type: "numeric(5,2)", notNull: true, default: 0 },
    years_smoked: { type: "numeric(5,2)", notNull: true, default: 0 },
    pack_years: { type: "numeric(7,2)", notNull: true, default: 0 },
    age_started_smoking: { type: "smallint" },
    secondhand_smoke_exposure: { type: "varchar(16)", notNull: true },
    tobacco_type: { type: "varchar(32)", notNull: true },
    other_tobacco_type: { type: "varchar(120)" },
    alcohol_consumption: { type: "varchar(16)", notNull: true },
    physical_activity: { type: "varchar(16)", notNull: true },
    weekly_physical_activity: { type: "varchar(32)", notNull: true },
    sleep_duration: { type: "varchar(16)", notNull: true },
    diet_quality: { type: "varchar(16)", notNull: true },
    height_cm: { type: "numeric(5,2)", notNull: true },
    weight_kg: { type: "numeric(6,2)", notNull: true },
    bmi: { type: "numeric(5,2)", notNull: true },
    air_pollution_exposure: { type: "varchar(16)", notNull: true },
    living_environment: { type: "varchar(16)", notNull: true },
    traffic_pollution_exposure: { type: "varchar(16)", notNull: true },
    residential_pollution_exposure: { type: "varchar(16)", notNull: true },
    occupational_exposure: { type: "varchar(16)", notNull: true },
    occupational_exposure_types: { type: "text[]", notNull: true, default: pgm.func("ARRAY[]::text[]") },
    occupational_exposure_duration: { type: "varchar(24)" },
    family_history_lung_cancer: { type: "varchar(16)", notNull: true },
    family_history_other_cancer: { type: "varchar(16)", notNull: true },
    family_cancer_relationship: { type: "varchar(32)" },
    created_at: { type: "timestamptz", notNull: true, default: pgm.func("CURRENT_TIMESTAMP") },
    updated_at: { type: "timestamptz", notNull: true, default: pgm.func("CURRENT_TIMESTAMP") },
  });

  pgm.addConstraint("lung_risk_assessments", "lung_risk_assessments_age_check", { check: "age BETWEEN 18 AND 120" });
  pgm.addConstraint("lung_risk_assessments", "lung_risk_assessments_smoking_check", { check: "smoking_status IN ('never', 'former', 'current')" });
  pgm.addConstraint("lung_risk_assessments", "lung_risk_assessments_numeric_check", { check: "cigarettes_per_day >= 0 AND years_smoked >= 0 AND pack_years >= 0 AND height_cm BETWEEN 80 AND 250 AND weight_kg BETWEEN 20 AND 400 AND bmi > 0" });
  pgm.addConstraint("lung_risk_assessments", "lung_risk_assessments_occupational_check", { check: "(occupational_exposure = 'yes' AND occupational_exposure_duration IS NOT NULL AND cardinality(occupational_exposure_types) > 0) OR (occupational_exposure IN ('no', 'unknown') AND cardinality(occupational_exposure_types) = 0)" });
  pgm.createIndex("lung_risk_assessments", "user_id");
  pgm.createIndex("lung_risk_assessments", ["user_id", "created_at"]);
};

exports.down = (pgm) => {
  pgm.dropTable("lung_risk_assessments");
  pgm.dropColumn("audit_events", "resource_id");
};
