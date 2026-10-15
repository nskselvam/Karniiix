export function requestContext(request, userId) {
  return {
    userId,
    requestId: request.id,
    ipAddress: request.ip,
    userAgent: request.headers["user-agent"],
  };
}

export function publicUser(user) {
  return {
    id: user.id,
    name: user.name,
    email: user.email,
    username: user.username ?? null,
    role: user.role,
  };
}
