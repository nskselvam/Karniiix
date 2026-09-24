import { ACCESS_COOKIE, REFRESH_COOKIE } from "../services/session-service.js";
import { AppError, unauthorized } from "../utils/app-error.js";

export function createAuthenticate({ authService, sessionService, config }) {
  return async function authenticate(request, reply) {
    try {
      await request.jwtVerify({ onlyCookie: true });
    } catch {
      const refreshed = await authService.rotate(request.cookies[REFRESH_COOKIE]);
      const accessToken = await reply.jwtSign(
        { sub: refreshed.user.id, role: refreshed.user.role },
        { sign: { expiresIn: config.accessTokenTtl } },
      );
      sessionService.setCookies(reply, accessToken, refreshed.refreshToken);
      request.user = { sub: refreshed.user.id, role: refreshed.user.role };
    }

    const currentUser = await authService.findCurrentUser(request.user.sub);
    if (!currentUser) throw unauthorized("SESSION_EXPIRED", "Session expired");
    request.currentUser = currentUser;
  };
}

export function requireRole(...allowedRoles) {
  return async function authorize(request) {
    if (!allowedRoles.includes(request.currentUser?.role)) {
      throw new AppError(403, "FORBIDDEN", "Access denied");
    }
  };
}

export { ACCESS_COOKIE, REFRESH_COOKIE };
