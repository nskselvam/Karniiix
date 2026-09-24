import { createHash, randomBytes } from "node:crypto";

export const ACCESS_COOKIE = "karnix_access";
export const REFRESH_COOKIE = "karnix_refresh";

export function hashToken(token) {
  return createHash("sha256").update(token).digest("hex");
}

export function createSessionService(config) {
  const cookieBase = {
    httpOnly: true,
    secure: config.cookieSecure,
    sameSite: "strict",
    path: "/",
  };

  return {
    createRefreshToken() {
      const token = randomBytes(48).toString("base64url");
      return {
        token,
        tokenHash: hashToken(token),
        expiresAt: new Date(Date.now() + config.refreshTokenTtlDays * 86_400_000),
      };
    },
    setCookies(reply, accessToken, refreshToken) {
      reply
        .setCookie(ACCESS_COOKIE, accessToken, { ...cookieBase, maxAge: config.accessTokenMaxAge })
        .setCookie(REFRESH_COOKIE, refreshToken, {
          ...cookieBase,
          maxAge: config.refreshTokenTtlDays * 86_400,
        });
    },
    clearCookies(reply) {
      reply.clearCookie(ACCESS_COOKIE, cookieBase).clearCookie(REFRESH_COOKIE, cookieBase);
    },
  };
}
