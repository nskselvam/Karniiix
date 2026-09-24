import { createAuditRepository } from "../repositories/audit-repository.js";
import { createRiskAssessmentRepository } from "../repositories/risk-assessment-repository.js";
import { normalizeAndCalculate } from "../utils/lung-risk-validation.js";

export function createRiskAssessmentService(database) {
  const assessments = createRiskAssessmentRepository(database);
  const audit = createAuditRepository(database);
  const context = (request, userId, resourceId, eventType) => ({
    userId,
    resourceId,
    eventType,
    requestId: request.id,
    ipAddress: request.ip,
    userAgent: request.headers["user-agent"],
  });

  return {
    async create(userId, input, request) {
      const data = normalizeAndCalculate(input);
      const assessment = await assessments.create(userId, data);
      await audit.record(context(request, userId, assessment.id, "RISK_PROFILE_CREATED"));
      return assessment;
    },
    list: (userId) => assessments.list(userId),
    find: (userId, id) => assessments.findById(userId, id),
    async update(userId, id, input, request) {
      const data = normalizeAndCalculate(input);
      const assessment = await assessments.update(userId, id, data);
      if (assessment) await audit.record(context(request, userId, id, "RISK_PROFILE_UPDATED"));
      return assessment;
    },
    async remove(userId, id, request) {
      const removed = await assessments.remove(userId, id);
      if (removed) await audit.record(context(request, userId, id, "RISK_PROFILE_DELETED"));
      return Boolean(removed);
    },
  };
}
