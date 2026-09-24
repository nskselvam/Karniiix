exports.up = (pgm) => {
  pgm.createExtension("pgcrypto", { ifNotExists: true });

  pgm.createTable("users", {
    id: { type: "uuid", primaryKey: true, default: pgm.func("gen_random_uuid()") },
    name: { type: "varchar(120)", notNull: true },
    email: { type: "varchar(254)", notNull: true, unique: true },
    password_hash: { type: "varchar(100)", notNull: true },
    role: { type: "varchar(32)", notNull: true, default: "user" },
    is_active: { type: "boolean", notNull: true, default: true },
    created_at: { type: "timestamptz", notNull: true, default: pgm.func("CURRENT_TIMESTAMP") },
    updated_at: { type: "timestamptz", notNull: true, default: pgm.func("CURRENT_TIMESTAMP") },
    last_login_at: { type: "timestamptz" },
    deleted_at: { type: "timestamptz" },
  });
  pgm.addConstraint("users", "users_role_check", { check: "role IN ('user')" });
  pgm.createIndex("users", "email", { name: "users_email_idx", unique: true });

  pgm.createTable("sessions", {
    id: { type: "uuid", primaryKey: true, default: pgm.func("gen_random_uuid()") },
    user_id: { type: "uuid", notNull: true, references: "users", onDelete: "CASCADE" },
    token_hash: { type: "char(64)", notNull: true, unique: true },
    expires_at: { type: "timestamptz", notNull: true },
    revoked_at: { type: "timestamptz" },
    replaced_by_session_id: { type: "uuid", references: "sessions" },
    created_at: { type: "timestamptz", notNull: true, default: pgm.func("CURRENT_TIMESTAMP") },
    last_used_at: { type: "timestamptz" },
  });
  pgm.createIndex("sessions", "user_id");
  pgm.createIndex("sessions", "expires_at");

  pgm.createTable("audit_events", {
    id: { type: "uuid", primaryKey: true, default: pgm.func("gen_random_uuid()") },
    user_id: { type: "uuid", references: "users", onDelete: "SET NULL" },
    event_type: { type: "varchar(64)", notNull: true },
    request_id: { type: "varchar(128)", notNull: true },
    ip_address: { type: "inet" },
    user_agent: { type: "varchar(512)" },
    metadata: { type: "jsonb", notNull: true, default: "{}" },
    created_at: { type: "timestamptz", notNull: true, default: pgm.func("CURRENT_TIMESTAMP") },
  });
  pgm.createIndex("audit_events", "user_id");
  pgm.createIndex("audit_events", "event_type");
  pgm.createIndex("audit_events", "created_at");
};

exports.down = (pgm) => {
  pgm.dropTable("audit_events");
  pgm.dropTable("sessions");
  pgm.dropTable("users");
};
