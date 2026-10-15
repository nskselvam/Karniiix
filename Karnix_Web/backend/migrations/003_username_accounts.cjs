exports.up = (pgm) => {
  pgm.addColumn("users", {
    username: { type: "varchar(32)" },
  });
  pgm.alterColumn("users", "email", { notNull: false });
  pgm.addConstraint("users", "users_username_format_check", {
    check: "username IS NULL OR username ~ '^[a-zA-Z0-9_-]{3,32}$'",
  });
  pgm.sql(
    "CREATE UNIQUE INDEX users_username_lower_idx ON users (lower(username)) WHERE username IS NOT NULL",
  );
};

exports.down = (pgm) => {
  pgm.sql("DROP INDEX users_username_lower_idx");
  pgm.dropConstraint("users", "users_username_format_check");
  pgm.dropColumn("users", "username");
  pgm.alterColumn("users", "email", { notNull: true });
};
