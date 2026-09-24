# Karnix Authentication API

Fastify and PostgreSQL service for registration, login, rotating sessions, current-user access, and account management. It intentionally contains no medical or ML domain code.

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
- `POST /api/v1/predictions`
- `GET /health`
- `GET /ready`
- `GET /docs`

## Security configuration

`BCRYPT_ROUNDS` defaults to 12. Passwords require 10-128 characters with uppercase, lowercase, and numeric characters. Production requires a 32+ character JWT secret, HTTPS CORS origin, secure cookies, and an explicit database URL.

The API emits structured Fastify request logs with request IDs. Audit rows never contain passwords, hashes, tokens, JWT secrets, or database credentials.

## Stage 2 feature contract

`src/config/lung-risk-features.js` is the central collection specification for the future model boundary. It does not run a model or produce a score. The backend calculates `pack_years` and `bmi`, validates conditional answers, and stores only the authenticated user's assessment. Every assessment query is scoped by both `user_id` and assessment UUID.

## Multi-disease prediction boundary

`GET /api/v1/predictions/catalog` lists the supported disease contracts. `POST /api/v1/predictions` requires authentication and accepts `diseaseId` plus a server-local `imagePath`; the browser never supplies a filesystem path outside `PREDICTION_INPUT_ROOT`. The only current executable adapter is the C++ lung baseline from `Other_Disease`. Brain tumor, osteoarthritis, lumbar stenosis, diabetes, and heart disease are listed as contract-only or model-unavailable until their validated artifacts and input contracts are supplied. No fake score is returned.
