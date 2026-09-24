import { expect, test } from "@playwright/test";

test("registers, restores the session, logs out, logs in, and protects welcome", async ({ page }) => {
  const email = `browser-${Date.now()}@example.com`;
  const password = "BrowserStrong1";

  await page.goto("/");
  await expect(page).toHaveURL(/\/login$/);
  await page.getByRole("link", { name: "Create an account" }).click();
  await expect(page).toHaveURL(/\/register$/);

  await page.getByLabel("Full Name").fill("Browser User");
  await page.getByLabel("Email").fill(email);
  await page.getByLabel("Password", { exact: true }).fill(password);
  await page.getByLabel("Confirm Password").fill(password);
  await page.getByRole("button", { name: "Create Account" }).click();
  await expect(page).toHaveURL(/\/welcome$/);
  await expect(page.getByRole("heading", { name: "Welcome, Browser User" })).toBeVisible();

  await page.reload();
  await expect(page.getByRole("heading", { name: "Welcome, Browser User" })).toBeVisible();
  const logoutResponsePromise = page.waitForResponse((response) => response.url().endsWith("/api/v1/auth/logout"));
  await page.getByRole("button", { name: "Logout" }).click();
  const logoutResponse = await logoutResponsePromise;
  expect(logoutResponse.status(), await logoutResponse.text()).toBe(200);
  await expect(page).toHaveURL(/\/login$/);

  await page.goto("/welcome");
  await expect(page).toHaveURL(/\/login$/);
  await page.getByLabel("Email").fill(email);
  await page.getByLabel("Password", { exact: true }).fill("Incorrect1");
  await page.getByRole("button", { name: "Login" }).click();
  await expect(page.getByRole("alert")).toHaveText("Invalid email or password.");

  await page.getByLabel("Password", { exact: true }).fill(password);
  await page.getByRole("button", { name: "Login" }).click();
  await expect(page).toHaveURL(/\/welcome$/);
});