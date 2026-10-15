import { createServer } from "node:http";
import { randomUUID } from "node:crypto";

const portFlag = process.argv.indexOf("--port");
const requestedPort =
  portFlag >= 0 ? Number.parseInt(process.argv[portFlag + 1], 10) : 0;
const assessments = [];
const requiredFields = [
  "age",
  "gender",
  "ethnicity",
  "smoking_status",
  "cigarettes_per_day",
  "years_smoked",
  "age_started_smoking",
  "secondhand_smoke_exposure",
  "tobacco_type",
  "other_tobacco_type",
  "alcohol_consumption",
  "physical_activity",
  "weekly_physical_activity",
  "sleep_duration",
  "diet_quality",
  "height_cm",
  "weight_kg",
  "air_pollution_exposure",
  "living_environment",
  "traffic_pollution_exposure",
  "residential_pollution_exposure",
  "occupational_exposure",
  "occupational_exposure_types",
  "occupational_exposure_duration",
  "family_history_lung_cancer",
  "family_history_other_cancer",
  "family_cancer_relationship",
];

function send(response, status, body, headers = {}) {
  response.writeHead(status, {
    "content-type": "application/json",
    ...headers,
  });
  response.end(JSON.stringify(body));
}

async function readBody(request) {
  let body = "";
  for await (const chunk of request) {
    body += chunk;
    if (body.length > 16 * 1024) throw new Error("Body too large");
  }
  return JSON.parse(body);
}

function authenticated(request) {
  return request.headers.cookie?.includes("karnix_access=test-session") ?? false;
}

const server = createServer(async (request, response) => {
  try {
    if (request.method === "POST" && request.url === "/api/v1/auth/login") {
      const input = await readBody(request);
      const identifierMatches = input.identifier?.toLowerCase() === "hyper";
      const passwordMatches = input.password === "karnix-test-pass";
      if (!identifierMatches || !passwordMatches) {
        console.log("LOGIN_REJECTED");
        send(response, 401, {
          success: false,
          error: { code: "INVALID_CREDENTIALS", message: "Invalid credentials." },
        });
        return;
      }
      console.log("LOGIN_OK");
      send(
        response,
        200,
        { success: true, data: { user: { email: input.email } } },
        {
          "set-cookie": [
            "karnix_access=test-session; Path=/; HttpOnly",
            "karnix_refresh=test-refresh; Path=/; HttpOnly",
          ],
        },
      );
      return;
    }

    if (!authenticated(request)) {
      send(response, 401, {
        success: false,
        error: { code: "UNAUTHORIZED", message: "Sign in first." },
      });
      return;
    }

    if (request.method === "GET" && request.url === "/api/v1/risk-assessments") {
      console.log(`LIST_COUNT ${assessments.length}`);
      send(response, 200, { success: true, data: assessments });
      return;
    }
    if (request.method === "POST" && request.url === "/api/v1/risk-assessments") {
      const input = await readBody(request);
      if (
        Object.keys(input).length !== requiredFields.length ||
        requiredFields.some((field) => !(field in input))
      ) {
        send(response, 400, {
          success: false,
          error: { code: "INVALID_RECORD", message: "Assessment fields are invalid." },
        });
        return;
      }
      const assessment = {
        ...input,
        id: randomUUID(),
        created_at: new Date().toISOString(),
        pack_years: 0,
        bmi: 24.2,
      };
      assessments.unshift(assessment);
      console.log("CREATE_OK");
      send(response, 201, { success: true, data: { assessment } });
      return;
    }
    if (request.method === "POST" && request.url === "/api/v1/auth/logout") {
      send(
        response,
        200,
        { success: true },
        {
          "set-cookie": [
            "karnix_access=; Path=/; Max-Age=0",
            "karnix_refresh=; Path=/; Max-Age=0",
          ],
        },
      );
      return;
    }
    send(response, 404, {
      success: false,
      error: { code: "NOT_FOUND", message: "Not found." },
    });
  } catch {
    send(response, 400, {
      success: false,
      error: { code: "INVALID_REQUEST", message: "Invalid request." },
    });
  }
});

server.listen(requestedPort, "127.0.0.1", () => {
  console.log(`LISTENING ${server.address().port}`);
});
