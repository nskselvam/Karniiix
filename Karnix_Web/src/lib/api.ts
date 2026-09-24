export interface User {
  id: string;
  name: string;
  email: string;
  role: "user";
}

export interface RiskAssessment {
  id: string;
  user_id: string;
  age: number;
  gender: string;
  ethnicity: string;
  smoking_status: string;
  cigarettes_per_day: number;
  years_smoked: number;
  pack_years: number;
  age_started_smoking: number | null;
  secondhand_smoke_exposure: string;
  tobacco_type: string;
  other_tobacco_type: string | null;
  alcohol_consumption: string;
  physical_activity: string;
  weekly_physical_activity: string;
  sleep_duration: string;
  diet_quality: string;
  height_cm: number;
  weight_kg: number;
  bmi: number;
  air_pollution_exposure: string;
  living_environment: string;
  traffic_pollution_exposure: string;
  residential_pollution_exposure: string;
  occupational_exposure: string;
  occupational_exposure_types: string[];
  occupational_exposure_duration: string | null;
  family_history_lung_cancer: string;
  family_history_other_cancer: string;
  family_cancer_relationship: string | null;
}

export interface DiseaseModelContract {
  id: string;
  label: string;
  modality: string;
  status: string;
  input: string;
  research_only: true;
  diagnosis: false;
}

interface ApiEnvelope<T> {
  success: true;
  data: T;
}

interface ApiErrorEnvelope {
  success: false;
  error: { code: string; message: string };
  request_id: string;
}

export class ApiError extends Error {
  constructor(
    public readonly status: number,
    public readonly code: string,
    message: string,
  ) {
    super(message);
  }
}

export const API_URL = process.env.NEXT_PUBLIC_API_URL ?? "http://localhost:4000";

async function request<T>(path: string, init: RequestInit = {}): Promise<T> {
  const controller = new AbortController();
  const timeout = window.setTimeout(() => controller.abort(), 10_000);
  const headers = new Headers(init.headers);
  if (init.body != null && !headers.has("Content-Type")) {
    headers.set("Content-Type", "application/json");
  }
  try {
    const response = await fetch(`${API_URL}${path}`, {
      ...init,
      credentials: "include",
      headers,
      signal: controller.signal,
    });
    const body = (await response.json()) as ApiEnvelope<T> | ApiErrorEnvelope;
    if (!response.ok || !body.success) {
      const error = body as ApiErrorEnvelope;
      throw new ApiError(response.status, error.error?.code ?? "REQUEST_FAILED", error.error?.message ?? "Request failed");
    }
    return body.data;
  } catch (error) {
    if (error instanceof ApiError) throw error;
    if (error instanceof DOMException && error.name === "AbortError") {
      throw new ApiError(0, "TIMEOUT", "The request timed out. Please try again.");
    }
    throw new ApiError(0, "NETWORK_ERROR", "Unable to connect. Please try again.");
  } finally {
    window.clearTimeout(timeout);
  }
}

export const api = {
  register: (input: { name: string; email: string; password: string; confirmPassword: string }) =>
    request<{ user: User }>("/api/v1/auth/register", { method: "POST", body: JSON.stringify(input) }),
  login: (input: { email: string; password: string }) =>
    request<{ user: User }>("/api/v1/auth/login", { method: "POST", body: JSON.stringify(input) }),
  me: () => request<{ user: User }>("/api/v1/auth/me"),
  logout: () => request<Record<string, never>>("/api/v1/auth/logout", { method: "POST" }),
  updateName: (name: string) =>
    request<{ user: User }>("/api/v1/users/me", { method: "PATCH", body: JSON.stringify({ name }) }),
  deactivate: () => request<Record<string, never>>("/api/v1/users/me", { method: "DELETE" }),
  createRiskAssessment: (assessment: Record<string, unknown>) =>
    request<{ assessment: RiskAssessment }>("/api/v1/risk-assessments", { method: "POST", body: JSON.stringify(assessment) }),
  listRiskAssessments: () => request<RiskAssessment[]>("/api/v1/risk-assessments"),
  getRiskAssessment: (id: string) => request<{ assessment: RiskAssessment }>(`/api/v1/risk-assessments/${id}`),
  updateRiskAssessment: (id: string, assessment: Record<string, unknown>) =>
    request<{ assessment: RiskAssessment }>(`/api/v1/risk-assessments/${id}`, { method: "PATCH", body: JSON.stringify(assessment) }),
  deleteRiskAssessment: (id: string) => request<Record<string, never>>(`/api/v1/risk-assessments/${id}`, { method: "DELETE" }),
  diseaseCatalog: () => request<{ diseases: DiseaseModelContract[] }>("/api/v1/predictions/catalog"),
  predictDisease: (diseaseId: string, imagePath: string) =>
    request<{ prediction: Record<string, unknown> }>("/api/v1/predictions", { method: "POST", body: JSON.stringify({ diseaseId, imagePath }) }),
  demoPredictions: () => request<{ predictions: Array<Record<string, unknown>>; demo: true }>("/api/v1/predictions/demo"),
};
