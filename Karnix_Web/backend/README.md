# Karnix Authentication API

Fastify and PostgreSQL service for registration, login, rotating sessions, current-user access, account management, research risk assessments, and fixed-registry image-model adapters.

## Architecture

HTTP routes validate strict JSON schemas and call controllers. Controllers call authentication/account services. Services own password and session policy and use parameterized repositories. PostgreSQL stores users, hashed opaque refresh sessions, and audit events.

Access tokens are short-lived JWTs in `HttpOnly`, `SameSite=Strict` cookies. Refresh tokens are 384-bit opaque values; only SHA-256 hashes are stored. Refresh use rotates and revokes the previous session. Logout, password change, and deactivation revoke sessions. Cross-origin state-changing requests are rejected and production cookies are `Secure`.

## Setup

1. Copy `backend/.env.example` to `backend/.env` and set a real PostgreSQL URL and random JWT secret.
2. Run `npm install` in `backend/`.
3. Run `npm run migrate` in `backend/`.
4. Run `npm run dev` in `backend/`.

From the frontend directory, the convenience equivalents are:

```sh
npm run db:migrate
npm run backend:dev
npm run backend:test
```

Rollback the latest migration with `npm run migrate:down` in `backend/` or `npm run db:rollback` from the frontend root.

## Endpoints

- `POST /api/v1/auth/register`
- `POST /api/v1/auth/login`
- `POST /api/v1/auth/logout`
- `GET /api/v1/auth/me`
- `GET /api/v1/users/me`
- `PATCH /api/v1/users/me`
- `DELETE /api/v1/users/me`
- `POST /api/v1/users/me/change-password`
- `POST /api/v1/risk-assessments`
- `GET /api/v1/risk-assessments`
- `GET /api/v1/risk-assessments/:id`
- `PATCH /api/v1/risk-assessments/:id`
- `DELETE /api/v1/risk-assessments/:id`
- `GET /api/v1/predictions/catalog`
- `GET /api/v1/predictions/demo`
- `POST /api/v1/predictions`
- `POST /api/v1/patient-assistant/ask`
- `GET /health`
- `GET /ready`
- `GET /docs`

Login accepts the existing account email or a local username. On interactive
first setup, `./setup.sh` provisions the email-less `Hyper` account after a
hidden password prompt; the password is validated against the normal policy
and stored only as a bcrypt hash. Email-less accounts have no email-based
password recovery. Non-interactive setup does not create the account.

## Security configuration

`BCRYPT_ROUNDS` defaults to 12. Passwords require 10-128 characters with uppercase, lowercase, and numeric characters. Production requires a 32+ character JWT secret, HTTPS CORS origin, secure cookies, and an explicit database URL.

The API emits structured Fastify request logs with request IDs. Audit rows never contain passwords, hashes, tokens, JWT secrets, or database credentials.

## Stage 2 feature contract

`src/config/lung-risk-features.js` defines the collection specification for the
lung-risk profile. It does not run a model or produce a score. The backend
calculates `pack_years` and `bmi`, validates conditional answers, and stores
only the authenticated user's assessment. Every assessment query is scoped by
both `user_id` and assessment UUID.

## Research image prediction boundary

The fixed registry has adapters for the lung X-ray baseline, four-class brain
MRI classifier, osteoarthritis X-ray classifier, and lumbar MRI classifier.
Osteoporosis DEXA, diabetes, and heart disease remain unavailable.
`GET /api/v1/predictions/catalog` reports registry metadata, checkpoint SHA-256,
and whether the configured executable and checkpoint files are present. Its
`configured` status does **not** prove checkpoint compatibility or clinical
validation; runtime compatibility is only established for an individual
request that successfully runs and passes output validation.

Authenticated `POST /api/v1/predictions` accepts `diseaseId` and a server-local
`imagePath` contained in `PREDICTION_INPUT_ROOT`. It invokes only the fixed
executable configured for that registry entry, with a timeout and bounded
output. It checks the image's file signature, validates the model's JSON
result, and returns its class and uncalibrated `model_score`; the score must
not be interpreted as a disease probability. The API does not upload browser
files yet.

Authenticated `GET /api/v1/predictions/demo` runs the configured repository
sample images through those same adapters. Results are explicitly marked as
research-demo outputs and are unrelated to the signed-in user's profile.
Missing images, missing checkpoints, failed execution, and invalid output are
reported as unavailable/error states, not fabricated predictions.

The model registry documents modality, input formats, preprocessing,
checkpoint identity, known provenance, any recorded evaluation, limitations,
and resource notes. The brain model's documented 44% held-out accuracy is
shown as a limitation, not as clinical validation. No doctor role, patient
case authorization, clinical report approval, DICOM workflow, GenAI copilot,
or medical-reference retrieval is implemented in this API.

Inference audit events record the task, model identifier, checkpoint hash,
execution outcome, and request context. They intentionally omit image paths,
model scores, and class outputs.

## Patient record assistant

The authenticated `POST /api/v1/patient-assistant/ask` endpoint accepts only a
`question` (maximum 1,000 characters); identity is derived from the existing
session cookie, never from a browser-supplied patient ID. It currently reads
only the signed-in user's `lung_risk_assessments` rows, selecting only fields
that match the question. Each SQL query is parameterized and scoped by
`user_id`. The selected record IDs and timestamps are sent with those fields
to the local Ollama service. Ollama is restricted to localhost. By default the peer-review panel uses
`medgemma1.5:4b` and the quantized OpenBioLLM 8B model below. Install them
explicitly, then verify with `ollama list`:

```sh
ollama pull medgemma1.5:4b
ollama pull hf.co/aaditya/OpenBioLLM-Llama3-8B-GGUF:Q4_K_M
```

Override `KARNIX_MODELS` with at least two distinct, already-installed model
names (exactly as shown by `ollama list`) to use another panel. The app never
downloads models. `KARNIX_MODEL` remains the single-model setting for the
separate C++ client.

For each supported question, every configured model first returns a
schema-constrained classification independently. The backend then sends each
model the peer models' classifications and asks it to review and return its
own final classification. The API continues only if all final
classifications agree exactly; disagreement or an invalid response is an
error, not a majority vote or a generated answer. This peer review is limited
to record-field selection and request classification—it does not produce
diagnoses, treatment decisions, or clinical conclusions. Keep the panel small
enough for the host's available memory and compute; very large models may
require substantial resources or exceed the inference timeout.

The model returns a constrained JSON classification and field-key selection,
not patient-facing prose. The API validates that every selected key belongs
to the queried field set, checks for conflicting saved values, and renders
the answer directly from PostgreSQL values. It never executes model-generated
SQL or accepts model-generated medical facts. A database error, unavailable
Ollama service, model disagreement, or invalid model output returns an error
instead of a success-shaped fallback. Assistant audit events record only the
outcome; questions and answer text are not stored.

The present schema has no appointment, allergy, medication, diagnosis, or
laboratory-result tables. Requests about those record types return the
controlled missing-information message. No patient records are seeded by
this feature; the risk-assessment form starts blank rather than pre-filling
example health values, and no migration is needed. Configure `OLLAMA_BASE_URL`
(local HTTP only) and `KARNIX_MODELS` in `backend/.env`. For a synthetic
engineering benchmark, run:

```sh
npm run evaluate:medical-models -- --models model-a,model-b
```

The evaluator uses four synthetic cases and the same independent-then-peer-
review protocol. It reports each model's initial and final exact-match
accuracy, schema-valid rate, latency, and the panel's unanimous-consensus
rate. These are software-task metrics, not medical accuracy or clinical
validation. Every requested model must already be installed in local Ollama.
