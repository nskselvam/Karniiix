const columns = `id, user_id, age, gender, ethnicity, smoking_status, cigarettes_per_day,
  years_smoked, pack_years, age_started_smoking, secondhand_smoke_exposure, tobacco_type,
  other_tobacco_type, alcohol_consumption, physical_activity, weekly_physical_activity,
  sleep_duration, diet_quality, height_cm, weight_kg, bmi, air_pollution_exposure,
  living_environment, traffic_pollution_exposure, residential_pollution_exposure,
  occupational_exposure, occupational_exposure_types, occupational_exposure_duration,
  family_history_lung_cancer, family_history_other_cancer, family_cancer_relationship,
  created_at, updated_at`;

const numericFields = ["cigarettes_per_day", "years_smoked", "pack_years", "height_cm", "weight_kg", "bmi"];
const presentNumbers = (row) => {
  if (!row) return row;
  const normalized = { ...row };
  for (const field of numericFields) normalized[field] = Number(normalized[field]);
  return normalized;
};

export function createRiskAssessmentRepository(database) {
  return {
    async create(userId, data) {
      const fields = Object.keys(data);
      const values = [userId, ...fields.map((field) => data[field])];
      const placeholders = values.map((_, index) => `$${index + 1}`);
      const { rows } = await database.query(
        `INSERT INTO lung_risk_assessments (user_id, ${fields.join(", ")})
         VALUES (${placeholders.join(", ")}) RETURNING ${columns}`,
        values,
      );
      return presentNumbers(rows[0]);
    },
    async list(userId) {
      const { rows } = await database.query(
        `SELECT ${columns} FROM lung_risk_assessments WHERE user_id = $1 ORDER BY created_at DESC`,
        [userId],
      );
      return rows.map(presentNumbers);
    },
    async findById(userId, id) {
      const { rows } = await database.query(
        `SELECT ${columns} FROM lung_risk_assessments WHERE user_id = $1 AND id = $2 LIMIT 1`,
        [userId, id],
      );
      return presentNumbers(rows[0] ?? null);
    },
    async update(userId, id, data) {
      const fields = Object.keys(data);
      const values = [userId, id, ...fields.map((field) => data[field])];
      const assignments = fields.map((field, index) => `${field} = $${index + 3}`);
      const { rows } = await database.query(
        `UPDATE lung_risk_assessments SET ${assignments.join(", ")}, updated_at = CURRENT_TIMESTAMP
         WHERE user_id = $1 AND id = $2 RETURNING ${columns}`,
        values,
      );
      return presentNumbers(rows[0] ?? null);
    },
    async remove(userId, id) {
      const { rows } = await database.query(
        "DELETE FROM lung_risk_assessments WHERE user_id = $1 AND id = $2 RETURNING id",
        [userId, id],
      );
      return rows[0] ?? null;
    },
  };
}
