import { buildApp } from "./app.js";
import { loadConfig } from "./config/env.js";

async function start() {
  try {
    const config = loadConfig();
    const app = await buildApp({ config });
    await app.listen({ host: config.host, port: config.port });
  } catch (error) {
    console.error("Authentication API failed to start:", error.message);
    process.exit(1);
  }
}

start();
