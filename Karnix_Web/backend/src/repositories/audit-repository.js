export function createAuditRepository(database) {
  return {
    async record({ userId = null, eventType, resourceId = null, requestId, ipAddress, userAgent, metadata = {} }) {
      await database.query(
        `INSERT INTO audit_events
          (user_id, event_type, resource_id, request_id, ip_address, user_agent, metadata)
         VALUES ($1, $2, $3, $4, $5, $6, $7)`,
        [userId, eventType, resourceId, requestId, ipAddress, userAgent?.slice(0, 512) || null, metadata],
      );
    },
  };
}
