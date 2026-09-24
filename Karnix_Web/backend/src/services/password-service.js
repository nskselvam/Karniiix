import bcrypt from "bcryptjs";
import { badRequest } from "../utils/app-error.js";

export function createPasswordService(rounds) {
  return {
    validate(password) {
      if (typeof password !== "string" || password.trim().length === 0) {
        throw badRequest("INVALID_PASSWORD", "Password is required");
      }
      if (password.length < 10 || password.length > 128) {
        throw badRequest("INVALID_PASSWORD", "Password must be between 10 and 128 characters");
      }
      if (!/[a-z]/.test(password) || !/[A-Z]/.test(password) || !/[0-9]/.test(password)) {
        throw badRequest("INVALID_PASSWORD", "Password must include uppercase, lowercase, and number characters");
      }
    },
    hash(password) {
      return bcrypt.hash(password, rounds);
    },
    compare(password, passwordHash) {
      return bcrypt.compare(password, passwordHash);
    },
  };
}
