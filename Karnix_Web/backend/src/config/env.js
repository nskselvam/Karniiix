import "dotenv/config";

const integer = (value, fallback, name) => {
  const parsed = Number.parseInt(value ?? String(fallback), 10);
  if (!Number.isInteger(parsed) || parsed <= 0) {
    throw new Error(`${name} must be a positive integer`);
  }
  return parsed;
};

export function loadConfig(env = process.env) {
  const isProduction = env.NODE_ENV === "production";
  const jwtSecret = env.JWT_SECRET ?? (isProduction ? "" : "development-only-change-me-32-characters");
  const databaseUrl = env.DATABASE_URL ?? "";
  const corsOrigin = env.CORS_ORIGIN ?? "http://localhost:3000";

  if (!databaseUrl) throw new Error("DATABASE_URL is required");
  if (jwtSecret.length < 32) throw new Error("JWT_SECRET must contain at least 32 characters");
  if (isProduction && !corsOrigin.startsWith("https://")) {
    throw new Error("CORS_ORIGIN must use HTTPS in production");
  }

  return Object.freeze({
    nodeEnv: env.NODE_ENV ?? "development",
    host: env.HOST ?? "127.0.0.1",
    port: integer(env.PORT, 4000, "PORT"),
    databaseUrl,
    databaseSsl: env.DATABASE_SSL === "true",
    corsOrigin,
    jwtSecret,
    jwtIssuer: env.JWT_ISSUER ?? "karnix-auth-api",
    jwtAudience: env.JWT_AUDIENCE ?? "karnix-web",
    accessTokenTtl: env.ACCESS_TOKEN_TTL ?? "15m",
    accessTokenMaxAge: integer(env.ACCESS_TOKEN_MAX_AGE_SECONDS, 900, "ACCESS_TOKEN_MAX_AGE_SECONDS"),
    refreshTokenTtlDays: integer(env.REFRESH_TOKEN_TTL_DAYS, 7, "REFRESH_TOKEN_TTL_DAYS"),
    bcryptRounds: integer(env.BCRYPT_ROUNDS, 12, "BCRYPT_ROUNDS"),
    authRateLimit: integer(env.AUTH_RATE_LIMIT, 10, "AUTH_RATE_LIMIT"),
    bodyLimit: integer(env.BODY_LIMIT_BYTES, 16384, "BODY_LIMIT_BYTES"),
    trustProxy: env.TRUST_PROXY === "true",
    cookieSecure: isProduction || env.COOKIE_SECURE === "true",
    predictionExecutable: env.PREDICTION_EXECUTABLE ?? "",
    predictionModelPath: env.PREDICTION_MODEL_PATH ?? "",
    predictionInputRoot: env.PREDICTION_INPUT_ROOT ?? "",
    osteoarthritisExecutable: env.OSTEOARTHRITIS_EXECUTABLE ?? "",
    osteoarthritisModelPath: env.OSTEOARTHRITIS_MODEL_PATH ?? "",
    brainTumorExecutable: env.BRAIN_TUMOR_EXECUTABLE ?? "",
    brainTumorModelPath: env.BRAIN_TUMOR_MODEL_PATH ?? "",
    lumbarStenosisExecutable: env.LUMBAR_STENOSIS_EXECUTABLE ?? "",
    lumbarStenosisModelPath: env.LUMBAR_STENOSIS_MODEL_PATH ?? "",
    demoLungImage: env.DEMO_LUNG_IMAGE ?? "",
    demoBrainTumorImage: env.DEMO_BRAIN_TUMOR_IMAGE ?? "",
    demoOsteoarthritisImage: env.DEMO_OSTEOARTHRITIS_IMAGE ?? "",
    demoLumbarStenosisImage: env.DEMO_LUMBAR_STENOSIS_IMAGE ?? "",
  });
}
