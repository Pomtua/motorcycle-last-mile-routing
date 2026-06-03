import { test, expect } from '@playwright/test';

test.describe('Motorcycle Routing Simulator E2E', () => {
  test.beforeEach(async ({ page }) => {
    page.on('console', msg => console.log('BROWSER LOG:', msg.text()));
    page.on('pageerror', err => console.log('BROWSER ERROR:', err.message));
    const responsePromise = page.waitForResponse('**/instances');
    await page.goto('http://localhost:3001');
    await responsePromise;
  });

  test('should complete standard user simulation journey', async ({ page }) => {
    await page.locator('select').first().selectOption('scale');
    
    const runBtn = page.locator('button:has-text("Run Simulation")');
    await expect(runBtn).toBeEnabled();
    await runBtn.click();

    const distanceVal = page.locator('p:has-text("km")').first();
    await expect(distanceVal).toBeVisible({ timeout: 15000 });
  });

  test('should display visual error banner on solver execution crashes', async ({ page }) => {
    await page.locator('select').first().selectOption('scale');
    
    await page.route('**/simulations', route => route.fulfill({
      status: 500,
      contentType: 'application/json',
      body: JSON.stringify({ message: "Simulation failed" }),
    }));

    await page.click('button:has-text("Run Simulation")');

    const err = page.locator('p:has-text("Simulation failed")');
    await expect(err).toBeVisible();
  });

  test('should toggle active riders sidebar and adapt map boundaries across viewports', async ({ page }) => {
    await page.locator('select').first().selectOption('scale');
    
    const runBtn = page.locator('button:has-text("Run Simulation")');
    await runBtn.click();

    const distanceVal = page.locator('p:has-text("km")').first();
    await expect(distanceVal).toBeVisible({ timeout: 15000 });

    await page.setViewportSize({ width: 1440, height: 900 });
    
    const toggleBtn = page.locator('button:has-text("Active Riders")');
    await expect(toggleBtn).toBeVisible();
    
    const riderLabel = page.locator('span:has-text("Rider #1")');
    await expect(riderLabel).toBeVisible();

    await toggleBtn.click();
    await expect(riderLabel).toBeHidden();

    await toggleBtn.click();
    await expect(riderLabel).toBeVisible();

    await page.setViewportSize({ width: 375, height: 667 });
    await expect(toggleBtn).toBeVisible();
  });
});
