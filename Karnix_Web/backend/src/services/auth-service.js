import { createAuditRepository } from "../repositories/audit-repository.js";
import { createSessionRepository } from "../repositories/session-repository.js";
import { createUserRepository } from "../repositories/user-repository.js";
import { withTransaction } from "../db/pool.js";
import { conflict, unauthorized } from "../utils/app-error.js";
import { hashToken } from "./session-service.js";

const INVALID_CREDENTIALS = "Invalid email or password.";
const DUMMY_HASH = "$2b$12$KIXxjSc32JQEV67f3mnXueLwQynCkn5dWG2VkWIebTpjfH1lL0V4S";

export const normalizeEmail = (email) => email.trim().toLowerCase();
export const normalizeName = (name) => name.trim().replace(/\s+/g, " ");

export function createAuthService({ pool, passwordService, sessionService }) {
  const users = createUserRepository(pool);
  const sessions = createSessionRepository(pool);
  const audit = createAuditRepository(pool);

  return {
    async register(input, context) {
      const name = normalizeName(input.name);
      const email = normalizeEmail(input.email);
      if (input.password !== input.confirmPassword) {
        throw conflict("PASSWORD_MISMATCH", "Passwords do not match");
      }
      passwordService.validate(input.password);
      if (await users.findAuthByEmail(email)) {
        throw conflict("EMAIL_EXISTS", "An account with this email already exists");
      }

      const passwordHash = await passwordService.hash(input.password);
      const refresh = sessionService.createRefreshToken();
      try {
        const user = await withTransaction(pool, async (client) => {
          const transactionUsers = createUserRepository(client);
          const created = await transactionUsers.create({ name, email, passwordHash });
          await createSessionRepository(client).create({
            userId: created.id,
            tokenHash: refresh.tokenHash,
            expiresAt: refresh.expiresAt,
          });
          await createAuditRepository(client).record({
            ...context,
            userId: created.id,
            eventType: "REGISTER",
          });
          return created;
        });
        return { user, refreshToken: refresh.token };
      } catch (error) {
        if (error?.code === "23505") {
          throw conflict("EMAIL_EXISTS", "An account with this email already exists");
        }
        throw error;
      }
    },

    async login(input, context) {
      const email = normalizeEmail(input.email);
      const user = await users.findAuthByEmail(email);
      const matches = await passwordService.compare(input.password, user?.password_hash ?? DUMMY_HASH);
      if (!user || !matches || !user.is_active || user.deleted_at) {
        await audit.record({ ...context, userId: user?.id, eventType: "LOGIN_FAILURE" });
        throw unauthorized("INVALID_CREDENTIALS", INVALID_CREDENTIALS);
      }

      const refresh = sessionService.createRefreshToken();
      await withTransaction(pool, async (client) => {
        await createSessionRepository(client).create({
          userId: user.id,
          tokenHash: refresh.tokenHash,
          expiresAt: refresh.expiresAt,
        });
        await createUserRepository(client).recordLogin(user.id);
        await createAuditRepository(client).record({ ...context, userId: user.id, eventType: "LOGIN_SUCCESS" });
      });
      delete user.password_hash;
      return { user, refreshToken: refresh.token };
    },

    async logout(refreshToken, context) {
      if (refreshToken) await sessions.revokeByTokenHash(hashToken(refreshToken));
      await audit.record({ ...context, userId: context.userId, eventType: "LOGOUT" });
    },

    async rotate(refreshToken) {
      if (!refreshToken) throw unauthorized("SESSION_EXPIRED", "Session expired");
      const activeSession = await sessions.findActive(hashToken(refreshToken));
      if (!activeSession) throw unauthorized("SESSION_EXPIRED", "Session expired");
      const user = await users.findById(activeSession.user_id);
      if (!user) {
        await sessions.revokeByTokenHash(hashToken(refreshToken));
        throw unauthorized("SESSION_EXPIRED", "Session expired");
      }
      const replacement = sessionService.createRefreshToken();
      await sessions.rotate({
        currentId: activeSession.id,
        userId: user.id,
        tokenHash: replacement.tokenHash,
        expiresAt: replacement.expiresAt,
      });
      return { user, refreshToken: replacement.token };
    },

    findCurrentUser(userId) {
      return users.findById(userId);
    },
  };
}
