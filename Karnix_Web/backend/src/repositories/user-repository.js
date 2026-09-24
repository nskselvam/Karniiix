const safeColumns = `
  id, name, email, role, is_active, created_at, updated_at, last_login_at, deleted_at
`;

export function createUserRepository(database) {
  return {
    async findAuthByEmail(email) {
      const { rows } = await database.query(
        `SELECT ${safeColumns}, password_hash FROM users WHERE email = $1 LIMIT 1`,
        [email],
      );
      return rows[0] ?? null;
    },

    async findAuthById(id) {
      const { rows } = await database.query(
        `SELECT ${safeColumns}, password_hash FROM users WHERE id = $1 LIMIT 1`,
        [id],
      );
      return rows[0] ?? null;
    },

    async findById(id) {
      const { rows } = await database.query(
        `SELECT ${safeColumns} FROM users WHERE id = $1 AND is_active = TRUE AND deleted_at IS NULL LIMIT 1`,
        [id],
      );
      return rows[0] ?? null;
    },

    async create({ name, email, passwordHash }) {
      const { rows } = await database.query(
        `INSERT INTO users (name, email, password_hash)
         VALUES ($1, $2, $3)
         RETURNING ${safeColumns}`,
        [name, email, passwordHash],
      );
      return rows[0];
    },

    async recordLogin(id) {
      await database.query(
        "UPDATE users SET last_login_at = CURRENT_TIMESTAMP, updated_at = CURRENT_TIMESTAMP WHERE id = $1",
        [id],
      );
    },

    async updateName(id, name) {
      const { rows } = await database.query(
        `UPDATE users SET name = $2, updated_at = CURRENT_TIMESTAMP
         WHERE id = $1 AND is_active = TRUE AND deleted_at IS NULL
         RETURNING ${safeColumns}`,
        [id, name],
      );
      return rows[0] ?? null;
    },

    async updatePassword(id, passwordHash) {
      await database.query(
        "UPDATE users SET password_hash = $2, updated_at = CURRENT_TIMESTAMP WHERE id = $1",
        [id, passwordHash],
      );
    },

    async deactivate(id) {
      const { rows } = await database.query(
        `UPDATE users SET is_active = FALSE, deleted_at = CURRENT_TIMESTAMP, updated_at = CURRENT_TIMESTAMP
         WHERE id = $1 AND deleted_at IS NULL RETURNING id`,
        [id],
      );
      return Boolean(rows[0]);
    },
  };
}
