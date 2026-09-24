import { createAuditRepository } from "../repositories/audit-repository.js";
import { createSessionRepository } from "../repositories/session-repository.js";
import { createUserRepository } from "../repositories/user-repository.js";
import { withTransaction } from "../db/pool.js";
import { badRequest, unauthorized } from "../utils/app-error.js";
import { normalizeName } from "./auth-service.js";

export function createAccountService({ pool, passwordService }) {
  const users = createUserRepository(pool);

  return {
    find(userId) {
      return users.findById(userId);
    },

    async updateName(userId, name, context) {
      const normalizedName = normalizeName(name);
      const user = await users.updateName(userId, normalizedName);
      if (!user) throw unauthorized();
      await createAuditRepository(pool).record({ ...context, userId, eventType: "ACCOUNT_UPDATE" });
      return user;
    },

    async changePassword(userId, input, context) {
      if (input.newPassword !== input.confirmNewPassword) {
        throw badRequest("PASSWORD_MISMATCH", "Passwords do not match");
      }
      passwordService.validate(input.newPassword);
      const currentUser = await users.findAuthById(userId);
      if (!currentUser || !(await passwordService.compare(input.currentPassword, currentUser.password_hash))) {
        throw unauthorized("INVALID_CURRENT_PASSWORD", "Current password is incorrect");
      }
      const passwordHash = await passwordService.hash(input.newPassword);
      await withTransaction(pool, async (client) => {
        await createUserRepository(client).updatePassword(userId, passwordHash);
        await createSessionRepository(client).revokeAllForUser(userId);
        await createAuditRepository(client).record({ ...context, userId, eventType: "PASSWORD_CHANGE" });
      });
    },

    async deactivate(userId, context) {
      await withTransaction(pool, async (client) => {
        await createUserRepository(client).deactivate(userId);
        await createSessionRepository(client).revokeAllForUser(userId);
        await createAuditRepository(client).record({ ...context, userId, eventType: "ACCOUNT_DEACTIVATION" });
      });
    },
  };
}
