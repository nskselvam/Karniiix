import assert from "node:assert/strict";
import { afterEach, beforeEach, describe, test } from "node:test";
import { buildApp } from "../src/app.js";
import { FakeDatabase } from "./fake-database.js";

const config = {
  nodeEnv: "test", host: "127.0.0.1", port: 4000, databaseUrl: "test", databaseSsl: false,
  corsOrigin: "http://localhost:3000", jwtSecret: "test-secret-with-at-least-thirty-two-characters",
  jwtIssuer: "karnix-auth-api", jwtAudience: "karnix-web", accessTokenTtl: "15m",
  accessTokenMaxAge: 900, refreshTokenTtlDays: 7, bcryptRounds: 4, authRateLimit: 100,
  bodyLimit: 1024, trustProxy: false, cookieSecure: false,
};

function cookieHeader(response) {
  const values = response.headers["set-cookie"] ?? [];
  return (Array.isArray(values) ? values : [values]).map((value) => value.split(";", 1)[0]).join("; ");
}

async function register(app, email = "user@example.com") {
  return app.inject({ method: "POST", url: "/api/v1/auth/register", headers: { origin: config.corsOrigin }, payload: { name: "Example User", email, password: "StrongPass1", confirmPassword: "StrongPass1" } });
}

describe("authentication and account API", () => {
  let app;
  let database;

  beforeEach(async () => {
    database = new FakeDatabase();
    app = await buildApp({ config, database, logger: false });
  });
  afterEach(async () => app.close());

  test("registers, normalizes email, hashes the password, and returns safe data", async () => {
    const response = await register(app, "  USER@Example.COM ");
    assert.equal(response.statusCode, 201);
    assert.equal(response.json().data.user.email, "user@example.com");
    assert.equal(response.json().data.user.password_hash, undefined);
    assert.match(database.users[0].password_hash, /^\$2[aby]\$/);
    assert.notEqual(database.users[0].password_hash, "StrongPass1");
    assert.equal(database.auditEvents[0].event_type, "REGISTER");
  });

  test("rejects duplicate email", async () => {
    await register(app);
    const response = await register(app);
    assert.equal(response.statusCode, 409);
    assert.equal(response.json().error.code, "EMAIL_EXISTS");
  });

  test("rejects invalid email, weak password, mismatch, and unexpected fields", async () => {
    const invalidEmail = await register(app, "not-an-email");
    assert.equal(invalidEmail.statusCode, 422);
    const weak = await app.inject({ method: "POST", url: "/api/v1/auth/register", payload: { name: "Test User", email: "test@example.com", password: "weak", confirmPassword: "weak" } });
    assert.equal(weak.statusCode, 400);
    const mismatch = await app.inject({ method: "POST", url: "/api/v1/auth/register", payload: { name: "Test User", email: "test@example.com", password: "StrongPass1", confirmPassword: "StrongPass2" } });
    assert.equal(mismatch.statusCode, 409);
    const extra = await app.inject({ method: "POST", url: "/api/v1/auth/login", payload: { email: "test@example.com", password: "StrongPass1", role: "admin" } });
    assert.equal(extra.statusCode, 422);
  });

  test("logs in with generic failures for wrong and unknown credentials", async () => {
    await register(app);
    const success = await app.inject({ method: "POST", url: "/api/v1/auth/login", payload: { email: "USER@example.com", password: "StrongPass1" } });
    assert.equal(success.statusCode, 200);
    for (const [email, password] of [["user@example.com", "WrongPass1"], ["missing@example.com", "WrongPass1"]]) {
      const failed = await app.inject({ method: "POST", url: "/api/v1/auth/login", payload: { email, password } });
      assert.equal(failed.statusCode, 401);
      assert.equal(failed.json().error.message, "Invalid email or password.");
    }
  });

  test("restores current user, updates account, and blocks privilege fields", async () => {
    const registration = await register(app);
    const cookie = cookieHeader(registration);
    const me = await app.inject({ method: "GET", url: "/api/v1/auth/me", headers: { cookie } });
    assert.equal(me.statusCode, 200);
    const update = await app.inject({ method: "PATCH", url: "/api/v1/users/me", headers: { cookie, origin: config.corsOrigin }, payload: { name: "Updated User" } });
    assert.equal(update.statusCode, 200);
    assert.equal(update.json().data.user.name, "Updated User");
    const escalation = await app.inject({ method: "PATCH", url: "/api/v1/users/me", headers: { cookie }, payload: { name: "Updated User", role: "admin" } });
    assert.equal(escalation.statusCode, 422);
  });

  test("rejects missing and invalid access tokens", async () => {
    const missing = await app.inject({ method: "GET", url: "/api/v1/auth/me" });
    assert.equal(missing.statusCode, 401);
    const invalid = await app.inject({ method: "GET", url: "/api/v1/auth/me", headers: { cookie: "karnix_access=invalid" } });
    assert.equal(invalid.statusCode, 401);
  });

  test("rotates refresh sessions when the access token is invalid", async () => {
    const registration = await register(app);
    const refresh = cookieHeader(registration).split("; ").find((part) => part.startsWith("karnix_refresh="));
    const response = await app.inject({ method: "GET", url: "/api/v1/auth/me", headers: { cookie: `karnix_access=invalid; ${refresh}` } });
    assert.equal(response.statusCode, 200);
    assert.equal(database.sessions.filter((session) => !session.revoked_at).length, 1);
    assert.equal(database.sessions.length, 2);
  });

  test("logout revokes the session and prevents subsequent access", async () => {
    const registration = await register(app);
    const cookie = cookieHeader(registration);
    const logout = await app.inject({ method: "POST", url: "/api/v1/auth/logout", headers: { cookie, origin: config.corsOrigin } });
    assert.equal(logout.statusCode, 200);
    const me = await app.inject({ method: "GET", url: "/api/v1/auth/me", headers: { cookie: "karnix_access=invalid" } });
    assert.equal(me.statusCode, 401);
    assert.ok(database.sessions.every((session) => session.revoked_at));
  });

  test("rejects a bodyless request falsely declared as JSON", async () => {
    const registration = await register(app);
    const response = await app.inject({
      method: "POST",
      url: "/api/v1/auth/logout",
      headers: { cookie: cookieHeader(registration), "content-type": "application/json" },
    });
    assert.equal(response.statusCode, 400);
  });

  test("changes password, revokes sessions, and deactivates accounts", async () => {
    const registration = await register(app);
    const cookie = cookieHeader(registration);
    const wrong = await app.inject({ method: "POST", url: "/api/v1/users/me/change-password", headers: { cookie }, payload: { currentPassword: "WrongPass1", newPassword: "NewStrong2", confirmNewPassword: "NewStrong2" } });
    assert.equal(wrong.statusCode, 401);
    const changed = await app.inject({ method: "POST", url: "/api/v1/users/me/change-password", headers: { cookie }, payload: { currentPassword: "StrongPass1", newPassword: "NewStrong2", confirmNewPassword: "NewStrong2" } });
    assert.equal(changed.statusCode, 200);
    const login = await app.inject({ method: "POST", url: "/api/v1/auth/login", payload: { email: "user@example.com", password: "NewStrong2" } });
    assert.equal(login.statusCode, 200);
    const deactivated = await app.inject({ method: "DELETE", url: "/api/v1/users/me", headers: { cookie: cookieHeader(login) } });
    assert.equal(deactivated.statusCode, 200);
    const denied = await app.inject({ method: "POST", url: "/api/v1/auth/login", payload: { email: "user@example.com", password: "NewStrong2" } });
    assert.equal(denied.statusCode, 401);
  });

  test("rejects SQL injection, malformed JSON, oversized bodies, and foreign origins", async () => {
    const injection = await app.inject({ method: "POST", url: "/api/v1/auth/login", payload: { email: "' OR 1=1 --", password: "StrongPass1" } });
    assert.equal(injection.statusCode, 422);
    const malformed = await app.inject({ method: "POST", url: "/api/v1/auth/login", headers: { "content-type": "application/json" }, payload: "{" });
    assert.equal(malformed.statusCode, 400);
    const oversized = await app.inject({ method: "POST", url: "/api/v1/auth/login", headers: { "content-type": "application/json" }, payload: JSON.stringify({ email: "a@example.com", password: "x".repeat(2000) }) });
    assert.equal(oversized.statusCode, 413);
    const csrf = await app.inject({ method: "POST", url: "/api/v1/auth/login", headers: { origin: "https://attacker.example" }, payload: { email: "user@example.com", password: "StrongPass1" } });
    assert.equal(csrf.statusCode, 403);
  });

  test("reports database readiness failure without internal details", async () => {
    database.failQueries = true;
    const response = await app.inject({ method: "GET", url: "/ready" });
    assert.equal(response.statusCode, 503);
    assert.deepEqual(response.json(), { status: "unavailable" });
  });

  test("rejects expired access tokens without a refresh session", async () => {
    const expired = app.jwt.sign(
      { sub: "b3b9ab0d-e8cb-41fc-84fa-a6d52f163803", role: "user" },
      { expiresIn: -1, iss: config.jwtIssuer, aud: config.jwtAudience },
    );
    const response = await app.inject({ method: "GET", url: "/api/v1/auth/me", headers: { cookie: `karnix_access=${expired}` } });
    assert.equal(response.statusCode, 401);
  });

  test("does not expose sensitive fields in successful or error responses", async () => {
    const registration = await register(app);
    const responses = [registration.body, (await app.inject({ method: "POST", url: "/api/v1/auth/login", payload: { email: "user@example.com", password: "bad" } })).body];
    for (const body of responses) {
      assert.doesNotMatch(body, /password_hash|StrongPass1|JWT_SECRET|DATABASE_URL/);
    }
  });

  test("rolls back registration when its audit event fails", async () => {
    database.failAudit = true;
    const response = await register(app);
    assert.equal(response.statusCode, 500);
    assert.equal(database.users.length, 0);
    assert.equal(database.sessions.length, 0);
  });
});

test("rate limits repeated login attempts", async () => {
  const database = new FakeDatabase();
  const app = await buildApp({ config: { ...config, authRateLimit: 2 }, database, logger: false });
  try {
    for (let attempt = 0; attempt < 2; attempt += 1) {
      const response = await app.inject({ method: "POST", url: "/api/v1/auth/login", payload: { email: "missing@example.com", password: "WrongPass1" } });
      assert.equal(response.statusCode, 401);
    }
    const limited = await app.inject({ method: "POST", url: "/api/v1/auth/login", payload: { email: "missing@example.com", password: "WrongPass1" } });
    assert.equal(limited.statusCode, 429);
    assert.equal(limited.json().error.code, "RATE_LIMITED");
  } finally {
    await app.close();
  }
});
