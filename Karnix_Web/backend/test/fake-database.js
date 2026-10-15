import { randomUUID } from "node:crypto";

export class FakeDatabase {
  constructor() {
    this.users = [];
    this.sessions = [];
    this.auditEvents = [];
    this.riskAssessments = [];
    this.failQueries = false;
  }

  async connect() {
    let snapshot;
    return {
      query: async (sql, params) => {
        const statement = sql.replace(/\s+/g, " ").trim();
        if (statement === "BEGIN") snapshot = structuredClone({ users: this.users, sessions: this.sessions, auditEvents: this.auditEvents });
        if (statement === "ROLLBACK" && snapshot) {
          this.users = snapshot.users;
          this.sessions = snapshot.sessions;
          this.auditEvents = snapshot.auditEvents;
        }
        return this.query(sql, params);
      },
      release() {},
    };
  }

  async query(sql, params = []) {
    if (this.failQueries) throw Object.assign(new Error("database unavailable"), { code: "ECONNREFUSED" });
    const statement = sql.replace(/\s+/g, " ").trim();
    if (["BEGIN", "COMMIT", "ROLLBACK", "SELECT 1"].includes(statement)) return { rows: [{ "?column?": 1 }], rowCount: 1 };

    if (statement.startsWith("SELECT") && statement.includes("FROM users WHERE email")) {
      const user = this.users.find((item) => item.email === params[0]);
      return { rows: user ? [{ ...user }] : [], rowCount: user ? 1 : 0 };
    }
    if (statement.startsWith("SELECT") && statement.includes("FROM users WHERE lower(username)")) {
      const user = this.users.find((item) => item.username?.toLowerCase() === params[0]);
      return { rows: user ? [{ ...user }] : [], rowCount: user ? 1 : 0 };
    }
    if (statement.startsWith("SELECT") && statement.includes("FROM users WHERE id")) {
      const user = this.users.find((item) => item.id === params[0] && (!statement.includes("is_active = TRUE") || (item.is_active && !item.deleted_at)));
      return { rows: user ? [{ ...user }] : [], rowCount: user ? 1 : 0 };
    }
    if (statement.startsWith("INSERT INTO users")) {
      if ((params[1] && this.users.some((item) => item.email === params[1])) ||
          (params[2] && this.users.some((item) => item.username?.toLowerCase() === params[2].toLowerCase()))) {
        throw Object.assign(new Error("duplicate"), { code: "23505" });
      }
      const now = new Date();
      const user = { id: randomUUID(), name: params[0], email: params[1], username: params[2], password_hash: params[3], role: "user", is_active: true, created_at: now, updated_at: now, last_login_at: null, deleted_at: null };
      this.users.push(user);
      return { rows: [{ ...user }], rowCount: 1 };
    }
    if (statement.startsWith("UPDATE users SET last_login_at")) {
      const user = this.users.find((item) => item.id === params[0]);
      if (user) user.last_login_at = new Date();
      return { rows: [], rowCount: user ? 1 : 0 };
    }
    if (statement.startsWith("UPDATE users SET name")) {
      const user = this.users.find((item) => item.id === params[0] && item.is_active && !item.deleted_at);
      if (user) user.name = params[1];
      return { rows: user ? [{ ...user }] : [], rowCount: user ? 1 : 0 };
    }
    if (statement.startsWith("UPDATE users SET password_hash")) {
      const user = this.users.find((item) => item.id === params[0]);
      if (user) user.password_hash = params[1];
      return { rows: [], rowCount: user ? 1 : 0 };
    }
    if (statement.startsWith("UPDATE users SET is_active")) {
      const user = this.users.find((item) => item.id === params[0] && !item.deleted_at);
      if (user) { user.is_active = false; user.deleted_at = new Date(); }
      return { rows: user ? [{ id: user.id }] : [], rowCount: user ? 1 : 0 };
    }

    if (statement.startsWith("INSERT INTO sessions")) {
      const session = { id: randomUUID(), user_id: params[0], token_hash: params[1], expires_at: new Date(params[2]), revoked_at: null, replaced_by_session_id: null };
      this.sessions.push(session);
      return { rows: [{ ...session }], rowCount: 1 };
    }
    if (statement.startsWith("SELECT") && statement.includes("FROM sessions")) {
      const session = this.sessions.find((item) => item.token_hash === params[0] && !item.revoked_at && item.expires_at > new Date());
      return { rows: session ? [{ ...session }] : [], rowCount: session ? 1 : 0 };
    }
    if (statement.startsWith("UPDATE sessions SET revoked_at") && statement.includes("replaced_by_session_id")) {
      const session = this.sessions.find((item) => item.id === params[0] && !item.revoked_at);
      if (session) { session.revoked_at = new Date(); session.replaced_by_session_id = params[1]; }
      return { rows: [], rowCount: session ? 1 : 0 };
    }
    if (statement.startsWith("UPDATE sessions SET revoked_at") && statement.includes("token_hash")) {
      const matches = this.sessions.filter((item) => item.token_hash === params[0] && !item.revoked_at);
      matches.forEach((item) => { item.revoked_at = new Date(); });
      return { rows: [], rowCount: matches.length };
    }
    if (statement.startsWith("UPDATE sessions SET revoked_at") && statement.includes("user_id")) {
      const matches = this.sessions.filter((item) => item.user_id === params[0] && !item.revoked_at);
      matches.forEach((item) => { item.revoked_at = new Date(); });
      return { rows: [], rowCount: matches.length };
    }
    if (statement.startsWith("INSERT INTO audit_events")) {
      if (this.failAudit) throw new Error("audit insert failed");
      this.auditEvents.push({ user_id: params[0], event_type: params[1], resource_id: params[2], request_id: params[3], ip_address: params[4], user_agent: params[5], metadata: params[6] });
      return { rows: [], rowCount: 1 };
    }
    if (statement.startsWith("SELECT") && statement.includes("FROM lung_risk_assessments")) {
      const selectedColumns = statement.slice("SELECT".length, statement.indexOf("FROM lung_risk_assessments"))
        .split(",")
        .map((column) => column.trim());
      const matching = this.riskAssessments
        .filter((assessment) => assessment.user_id === params[0])
        .sort((left, right) => new Date(right.created_at) - new Date(left.created_at))
        .slice(0, 20)
        .map((assessment) => Object.fromEntries(
          selectedColumns.map((column) => [column, assessment[column]]),
        ));
      return { rows: matching, rowCount: matching.length };
    }
    throw new Error(`Unhandled fake SQL: ${statement}`);
  }
}
