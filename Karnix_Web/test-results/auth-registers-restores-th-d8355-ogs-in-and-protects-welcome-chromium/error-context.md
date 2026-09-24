# Instructions

- Following Playwright test failed.
- Explain why, be concise, respect Playwright best practices.
- Provide a snippet of code with the fix, if possible.

# Test info

- Name: auth.spec.ts >> registers, restores the session, logs out, logs in, and protects welcome
- Location: tests/e2e/auth.spec.ts:3:5

# Error details

```
Error: locator.fill: Error: strict mode violation: getByLabel('Password') resolved to 2 elements:
    1) <input required="" id="password" type="password" name="password" autocomplete="current-password" class="w-full border-0 border-b border-slate-800/60 bg-transparent pb-2 pr-10 text-lg text-slate-950 outline-none focus:border-slate-950"/> aka getByRole('textbox', { name: 'Password' })
    2) <button type="button" aria-label="Show password" class="absolute right-0 top-0 p-1 text-slate-900">…</button> aka getByRole('button', { name: 'Show password' })

Call log:
  - waiting for getByLabel('Password')

```

# Page snapshot

```yaml
- generic [ref=f2e1]:
  - button "Open Next.js Dev Tools" [ref=f2e7] [cursor=pointer]
  - alert [ref=f2e11]
  - main [ref=f2e24]:
    - region [ref=f2e25]:
      - heading "Login" [level=1] [ref=f2e27]
      - generic [ref=f2e28]:
        - generic [ref=f2e29]:
          - generic [ref=f2e30]: Email
          - textbox "Email" [active] [ref=f2e31]: browser-1789103166543@example.com
        - generic [ref=f2e32]:
          - generic [ref=f2e33]: Password
          - generic [ref=f2e34]:
            - textbox "Password" [ref=f2e35]
            - button "Show password" [ref=f2e36]
        - button "Login" [ref=f2e40]
        - generic [ref=f2e41]:
          - text: New here?
          - link "Create an account" [ref=f2e42] [cursor=pointer]:
            - /url: /register
```

# Test source

```ts
  1  | import { expect, test } from "@playwright/test";
  2  | 
  3  | test("registers, restores the session, logs out, logs in, and protects welcome", async ({ page }) => {
  4  |   const email = `browser-${Date.now()}@example.com`;
  5  |   const password = "BrowserStrong1";
  6  | 
  7  |   await page.goto("/");
  8  |   await expect(page).toHaveURL(/\/login$/);
  9  |   await page.getByRole("link", { name: "Create an account" }).click();
  10 |   await expect(page).toHaveURL(/\/register$/);
  11 | 
  12 |   await page.getByLabel("Full Name").fill("Browser User");
  13 |   await page.getByLabel("Email").fill(email);
  14 |   await page.getByLabel("Password", { exact: true }).fill(password);
  15 |   await page.getByLabel("Confirm Password").fill(password);
  16 |   await page.getByRole("button", { name: "Create Account" }).click();
  17 |   await expect(page).toHaveURL(/\/welcome$/);
  18 |   await expect(page.getByRole("heading", { name: "Welcome, Browser User" })).toBeVisible();
  19 | 
  20 |   await page.reload();
  21 |   await expect(page.getByRole("heading", { name: "Welcome, Browser User" })).toBeVisible();
  22 |   const logoutResponsePromise = page.waitForResponse((response) => response.url().endsWith("/api/v1/auth/logout"));
  23 |   await page.getByRole("button", { name: "Logout" }).click();
  24 |   const logoutResponse = await logoutResponsePromise;
  25 |   expect(logoutResponse.status(), await logoutResponse.text()).toBe(200);
  26 |   await expect(page).toHaveURL(/\/login$/);
  27 | 
  28 |   await page.goto("/welcome");
  29 |   await expect(page).toHaveURL(/\/login$/);
  30 |   await page.getByLabel("Email").fill(email);
> 31 |   await page.getByLabel("Password").fill("Incorrect1");
     |                                     ^ Error: locator.fill: Error: strict mode violation: getByLabel('Password') resolved to 2 elements:
  32 |   await page.getByRole("button", { name: "Login" }).click();
  33 |   await expect(page.getByRole("alert")).toHaveText("Invalid email or password.");
  34 | 
  35 |   await page.getByLabel("Password").fill(password);
  36 |   await page.getByRole("button", { name: "Login" }).click();
  37 |   await expect(page).toHaveURL(/\/welcome$/);
  38 | });
```