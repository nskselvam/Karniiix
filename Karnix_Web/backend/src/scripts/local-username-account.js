import { randomUUID } from "node:crypto";
import { loadConfig } from "../config/env.js";
import { createPool, withTransaction } from "../db/pool.js";
import { createAuditRepository } from "../repositories/audit-repository.js";
import { createUserRepository } from "../repositories/user-repository.js";
import { createPasswordService } from "../services/password-service.js";

const [action, rawUsername] = process.argv.slice(2);
const username = rawUsername?.trim().toLowerCase();

if (
  !["--exists", "--create"].includes(action) ||
  !username ||
  !/^[a-z0-9_-]{3,32}$/.test(username)
) {
  console.error("Usage: local-username-account.js --exists|--create <username>");
  process.exit(2);
}

const config = loadConfig();
const pool = createPool(config);

try {
  const { rows } = await pool.query(
    "SELECT id FROM users WHERE lower(username) = $1 LIMIT 1",
    [username],
  );
  if (rows.length > 0) {
    console.log(`Local username '${username}' already exists; leaving it unchanged.`);
    process.exitCode = action === "--exists" ? 0 : 1;
  } else if (action === "--exists") {
    console.log(`Local username '${username}' does not exist.`);
    process.exitCode = 1;
  } else {
    let input = "";
    for await (const chunk of process.stdin) {
      input += chunk;
      if (input.length > 260) throw new Error("Password input is too long.");
    }
    const [password, confirmation, ...extra] = input.split("\n");
    if (!password || password !== confirmation || extra.some(Boolean)) {
      throw new Error("Password confirmation did not match.");
    }

    const passwords = createPasswordService(config.bcryptRounds);
    passwords.validate(password);
    const passwordHash = await passwords.hash(password);
    await withTransaction(pool, async (client) => {
      const user = await createUserRepository(client).create({
        name: rawUsername.trim(),
        username,
        passwordHash,
      });
      await createAuditRepository(client).record({
        userId: user.id,
        requestId: randomUUID(),
        eventType: "LOCAL_USERNAME_ACCOUNT_CREATED",
        ipAddress: null,
        userAgent: null,
      });
    });
    console.log(`Local username '${username}' was created. The password is not stored in plaintext.`);
  }
} catch (error) {
  console.error(`Could not provision local username '${username}': ${error.message}`);
  process.exitCode = 1;
} finally {
  await pool.end();
}
