#!/usr/bin/env node
// One-time CareLink login for the Pumpkin bridge.
//
//   npm run login                 log in, store the tokens in the Worker's KV
//   npm run login -- --out f.json write them to a file instead
//   npm run login -- --country NL login region from another country (default BE)
//
// Opens your own Google Chrome (no extra browser download) on the CareLink
// login page. Log in with the care-partner account and solve the captcha; the
// tool catches the redirect back to the CareLink app, exchanges the code for
// tokens and stores them as KV key "tokens" with Wrangler. Run it from bridge/.

import { spawnSync } from 'node:child_process';
import { mkdtempSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { authorizeUrl, codeFromRedirect, exchangeCode, resolveLoginConfig, DEFAULT_DISCOVERY_URL } from './carelink-login-lib.mjs';

const LOGIN_TIMEOUT_MS = 10 * 60 * 1000;

function parseArgs(argv) {
  const opts = { out: null, country: 'BE', region: null, discoveryUrl: DEFAULT_DISCOVERY_URL, kv: true };
  for (let i = 0; i < argv.length; i++) {
    const a = argv[i];
    const next = () => { if (i + 1 >= argv.length) throw new Error(`${a} needs a value`); return argv[++i]; };
    if (a === '--out') { opts.out = next(); opts.kv = false; }
    else if (a === '--country') opts.country = next();
    else if (a === '--us') opts.region = 'US';
    else if (a === '--discovery') opts.discoveryUrl = next();
    else if (a === '-h' || a === '--help') { opts.help = true; }
    else throw new Error(`unknown option ${a}`);
  }
  return opts;
}

async function captureCode(cfg) {
  let chromium;
  try {
    ({ chromium } = await import('playwright-core'));
  } catch {
    throw new Error('playwright-core is missing: run "npm install" in bridge/ first');
  }
  let browser;
  try {
    browser = await chromium.launch({ channel: 'chrome', headless: false });
  } catch (e) {
    throw new Error(`could not start Google Chrome (${e.message.split('\n')[0]}). Install Chrome, or close a Chrome that hangs.`);
  }
  try {
    const context = await browser.newContext();
    const page = await context.newPage();
    const code = await new Promise((resolve, reject) => {
      const timer = setTimeout(() => reject(new Error('no login within 10 minutes')), LOGIN_TIMEOUT_MS);
      const found = (url) => {
        const c = codeFromRedirect(url, cfg.redirectUri);
        if (c) { clearTimeout(timer); resolve(c); }
      };
      // The last step is a redirect to the app's URL scheme (com.medtronic…:/…?code=…),
      // which Chrome can't open. Catch it from the redirect response or the request.
      context.on('response', (res) => {
        const status = res.status();
        if (status >= 300 && status < 400) found(res.headers().location);
      });
      context.on('request', (req) => found(req.url()));
      context.on('requestfailed', (req) => found(req.url()));
      page.on('close', () => { clearTimeout(timer); reject(new Error('the login window was closed')); });
      page.goto(authorizeUrl(cfg)).catch(() => { /* navigation to the app scheme also "fails" */ });
    });
    return code;
  } finally {
    await browser.close().catch(() => {});
  }
}

function wrangler(args, { input } = {}) {
  const res = spawnSync('npx', ['wrangler', ...args], { stdio: input ? ['pipe', 'inherit', 'inherit'] : 'inherit', input, shell: process.platform === 'win32' });
  return res.status === 0;
}

async function main() {
  const opts = parseArgs(process.argv.slice(2));
  if (opts.help) {
    console.log('Usage: npm run login [-- --out tokens.json] [-- --country BE] [-- --us]');
    return;
  }
  console.log('Looking up the CareLink login…');
  const cfg = await resolveLoginConfig({ discoveryUrl: opts.discoveryUrl, country: opts.country, region: opts.region });
  console.log(`Region ${cfg.region}. A Chrome window opens: log in with the CARE-PARTNER account and solve the captcha.`);
  const code = await captureCode(cfg);
  console.log('Login done, fetching tokens…');
  const tokens = await exchangeCode(cfg, code);
  const json = JSON.stringify(tokens);

  if (opts.out) {
    writeFileSync(opts.out, json, { mode: 0o600 });
    console.log(`Tokens written to ${opts.out}. It gives access to the patient's data: delete it once it's in KV.`);
    return;
  }

  const dir = mkdtempSync(join(tmpdir(), 'pumpkin-login-'));
  const file = join(dir, 'tokens.json');
  try {
    writeFileSync(file, json, { mode: 0o600 });
    console.log('Storing the tokens in the Worker (KV key "tokens")…');
    if (!wrangler(['kv', 'key', 'put', 'tokens', '--path', file, '--binding', 'CARELINK', '--remote'])) {
      throw new Error('wrangler could not store the tokens. Check "npx wrangler whoami" and that you run this in bridge/');
    }
    // Fresh start with the new login: look up the account again (endpoints, patient)
    // and drop the error state, so a login backoff (up to 60 min) doesn't delay it.
    for (const key of ['config', 'status']) {
      wrangler(['kv', 'key', 'delete', key, '--binding', 'CARELINK', '--remote']);
    }
  } finally {
    rmSync(dir, { recursive: true, force: true });
  }
  console.log('\nDone. The bridge uses the new login from its next poll (within a minute when a clock asks).');
  console.log('A "key not found" message from the delete steps above is fine.');
  console.log('Check: curl -s -H "Authorization: Bearer $CLOCK_TOKEN" https://<your-worker>/debug');
}

main().catch((e) => {
  console.error(`\nLogin failed: ${e.message}`);
  process.exit(1);
});
