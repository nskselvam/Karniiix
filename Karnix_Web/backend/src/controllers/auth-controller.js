import { REFRESH_COOKIE } from "../middleware/authenticate.js";
import { publicUser, requestContext } from "../utils/request-context.js";

async function establishSession(request, reply, result) {
  const accessToken = await reply.jwtSign(
    { sub: result.user.id, role: result.user.role },
    { sign: { expiresIn: request.server.config.accessTokenTtl } },
  );
  request.server.sessionService.setCookies(reply, accessToken, result.refreshToken);
  return { success: true, data: { user: publicUser(result.user) } };
}

export function createAuthController({ authService, sessionService }) {
  return {
    register: async (request, reply) => {
      const result = await authService.register(request.body, requestContext(request));
      reply.code(201);
      return establishSession(request, reply, result);
    },
    login: async (request, reply) => {
      const result = await authService.login(request.body, requestContext(request));
      return establishSession(request, reply, result);
    },
    logout: async (request, reply) => {
      await authService.logout(
        request.cookies[REFRESH_COOKIE],
        requestContext(request, request.currentUser.id),
      );
      sessionService.clearCookies(reply);
      return { success: true };
    },
    me: async (request) => ({
      success: true,
      data: { user: publicUser(request.currentUser) },
    }),
  };
}
