export function createSessionRepository(database) {
  return {
    async create({ userId, tokenHash, expiresAt }) {
      const { rows } = await database.query(
        `INSERT INTO sessions (user_id, token_hash, expires_at)
         VALUES ($1, $2, $3) RETURNING id, user_id, expires_at`,
        [userId, tokenHash, expiresAt],
      );
      return rows[0];
    },

    async findActive(tokenHash) {
      const { rows } = await database.query(
        `SELECT id, user_id, expires_at FROM sessions
         WHERE token_hash = $1 AND revoked_at IS NULL AND expires_at > CURRENT_TIMESTAMP LIMIT 1`,
        [tokenHash],
      );
      return rows[0] ?? null;
    },

    async rotate({ currentId, userId, tokenHash, expiresAt }) {
      const { rows } = await database.query(
        `INSERT INTO sessions (user_id, token_hash, expires_at)
         VALUES ($1, $2, $3) RETURNING id, user_id, expires_at`,
        [userId, tokenHash, expiresAt],
      );
      await database.query(
        `UPDATE sessions SET revoked_at = CURRENT_TIMESTAMP, replaced_by_session_id = $2,
         last_used_at = CURRENT_TIMESTAMP WHERE id = $1 AND revoked_at IS NULL`,
        [currentId, rows[0].id],
      );
      return rows[0];
    },

    async revokeByTokenHash(tokenHash) {
      await database.query(
        "UPDATE sessions SET revoked_at = CURRENT_TIMESTAMP WHERE token_hash = $1 AND revoked_at IS NULL",
        [tokenHash],
      );
    },

    async revokeAllForUser(userId) {
      await database.query(
        "UPDATE sessions SET revoked_at = CURRENT_TIMESTAMP WHERE user_id = $1 AND revoked_at IS NULL",
        [userId],
      );
    },
  };
}
