import { publicUser, requestContext } from "../utils/request-context.js";

export function createUserController({ accountService, sessionService }) {
  return {
    get: async (request) => ({
      success: true,
      data: { user: publicUser(request.currentUser) },
    }),
    update: async (request) => {
      const user = await accountService.updateName(
        request.currentUser.id,
        request.body.name,
        requestContext(request),
      );
      return { success: true, data: { user: publicUser(user) } };
    },
    changePassword: async (request, reply) => {
      await accountService.changePassword(
        request.currentUser.id,
        request.body,
        requestContext(request),
      );
      sessionService.clearCookies(reply);
      return { success: true };
    },
    deactivate: async (request, reply) => {
      await accountService.deactivate(request.currentUser.id, requestContext(request));
      sessionService.clearCookies(reply);
      return { success: true };
    },
  };
}
