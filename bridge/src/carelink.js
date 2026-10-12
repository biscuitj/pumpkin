// CareLink care-partner client for the bridge.
//
// Flow (same as the CareLink Connect app, xDrip+ and carelink-python-client):
//   discovery -> SSO config -> token refresh -> users/me (+ profile, linked patient)
//   -> POST {baseUrlCumulus}/display/message -> patientData
//
// State in KV (binding CARELINK):
//   tokens   - logindata.json from the one-time browser login; refresh token rotates
//   config   - discovery + identity, refreshed daily
//   snapshot - compact patient data, written only when something changed
//   status   - errors and backoff, written only when the state changes

import { buildSnapshot, snapshotFingerprint, updateLatches, detectSetChange } from './carelink-data.js';

const MIN = 60 * 1000;
const DEFAULT_DISCOVERY_URL = 'https://clcloud.minimed.eu/connect/carepartner/v13/discover/android/3.8';
const APP_VERSION = '3.8.0';
const USER_AGENT = 'Dalvik/2.1.0 (Linux; U; Android 10; Nexus 5X Build/QQ3A.200805.001)';
const TIMEOUT_MS = 10000;
const CONFIG_TTL_MS = 24 * 60 * MIN;
const REFRESH_BEFORE_MS = 10 * MIN;
const REFRESH_AT_LEAST_EVERY_MS = 24 * 60 * MIN; // keeps the refresh token alive
const SNAPSHOT_REWRITE_MS = 30 * MIN; // refresh fetched_at even when nothing changed

export class CareLinkAuthError extends Error {
  constructor(message) { super(message); this.name = 'CareLinkAuthError'; }
}

export function decodeJwtPayload(token) {
  try {
    const part = String(token).split('.')[1];
    if (!part) return null;
    const b64 = part.replace(/-/g, '+').replace(/_/g, '/').padEnd(Math.ceil(part.length / 4) * 4, '=');
    return JSON.parse(atob(b64));
  } catch (e) {
    return null;
  }
}

/** When the access token was issued (JWT iat), 0 if unknown. */
export function tokenIssuedAt(tokens) {
  const p = decodeJwtPayload(tokens.access_token);
  return p && Number.isFinite(p.iat) ? p.iat * 1000 : 0;
}

/** Short, token-free description of an error response, for /debug. */
async function describeError(res) {
  let text = '';
  try { text = await res.text(); } catch (e) { /* ignore */ }
  let json = null;
  try { json = JSON.parse(text); } catch (e) { /* not JSON */ }
  if (json && (json.error || json.error_description || json.message)) {
    const parts = [json.error, json.error_description || json.message].filter(Boolean).map(String);
    return { json: true, code: json.error ? String(json.error) : null, detail: parts.join(': ').slice(0, 200) };
  }
  const type = res.headers.get('content-type') || 'unknown type';
  const snippet = text.replace(/<[^>]*>/g, ' ').replace(/\s+/g, ' ').trim().slice(0, 120);
  return { json: false, code: null, detail: `${type}${snippet ? `: ${snippet}` : ''}` };
}

// OAuth errors that mean the login itself is no longer valid.
const LOGIN_ERRORS = new Set(['invalid_grant', 'invalid_client', 'unauthorized_client', 'access_denied', 'invalid_token', 'login_required']);

export function tokenExpiresAt(tokens) {
  if (Number.isFinite(tokens.expires_at)) return tokens.expires_at;
  const p = decodeJwtPayload(tokens.access_token);
  return p && Number.isFinite(p.exp) ? p.exp * 1000 : 0;
}

function timeout() { return AbortSignal.timeout(TIMEOUT_MS); }

async function getJson(url, fetchImpl) {
  const res = await fetchImpl(url, { headers: { Accept: 'application/json', 'User-Agent': USER_AGENT }, signal: timeout() });
  if (!res.ok) throw new Error(`GET ${url} -> HTTP ${res.status}`);
  return res.json();
}

function apiHeaders(tokens, json) {
  const h = { Accept: 'application/json', 'User-Agent': USER_AGENT, Authorization: `Bearer ${tokens.access_token}` };
  if (json) h['Content-Type'] = 'application/json';
  if (tokens['mag-identifier']) h['mag-identifier'] = tokens['mag-identifier'];
  return h;
}

/** Discovery + SSO config -> endpoints for the account's region. */
export async function discover(env, tokens, fetchImpl) {
  const url = env.CARELINK_DISCOVERY_URL || DEFAULT_DISCOVERY_URL;
  const disco = await getJson(url, fetchImpl);
  const payload = decodeJwtPayload(tokens.access_token) || {};
  const country = String(env.CARELINK_COUNTRY || (payload.token_details && payload.token_details.country) || 'BE').toUpperCase();
  let region = null;
  for (const c of disco.supportedCountries || []) {
    if (c && c[country] && c[country].region) { region = c[country].region; break; }
  }
  region = String(region || 'EU').toUpperCase();
  const cp = (disco.CP || []).find((c) => String(c.region || '').toUpperCase() === region);
  if (!cp) throw new Error(`CareLink discovery has no config for region ${region}`);
  const ssoKey = cp.UseSSOConfiguration;
  const ssoUrl = (ssoKey && cp[ssoKey]) || cp.SSOConfiguration;
  if (!ssoUrl) throw new Error('CareLink discovery has no SSO configuration URL');
  const sso = await getJson(ssoUrl, fetchImpl);
  const s = sso.server || {};
  let base = `https://${s.hostname}:${s.port}/${s.prefix || ''}`;
  if (base.endsWith('/')) base = base.slice(0, -1);
  const tokenPath = sso.system_endpoints && sso.system_endpoints.token_endpoint_path;
  if (!s.hostname || !tokenPath) throw new Error('CareLink SSO configuration is incomplete');
  return {
    discovery_url: url,
    country,
    region,
    token_url: base + tokenPath,
    base_carelink: String(cp.baseUrlCareLink || '').replace(/\/+$/, ''),
    base_cumulus: String(cp.baseUrlCumulus || '').replace(/\/+$/, ''),
  };
}

/** Exchange the refresh token. The new refresh token is stored before anything else. */
export async function refreshTokens(kv, config, tokens, now, fetchImpl) {
  const form = new URLSearchParams({
    refresh_token: tokens.refresh_token,
    client_id: tokens.client_id,
    grant_type: 'refresh_token',
  });
  if (tokens.client_secret) form.set('client_secret', tokens.client_secret);
  const headers = {
    'Content-Type': 'application/x-www-form-urlencoded; charset=UTF-8',
    Accept: 'application/json',
    'User-Agent': USER_AGENT,
  };
  if (tokens['mag-identifier']) headers['mag-identifier'] = tokens['mag-identifier'];
  const res = await fetchImpl(config.token_url, { method: 'POST', headers, body: form.toString(), signal: timeout() });
  if (!res.ok) {
    const err = await describeError(res);
    const where = `token refresh at ${new URL(config.token_url).host}`;
    // A JSON OAuth error (or a bare 400/401) means the login is gone. A 403 without
    // an OAuth error body is usually a firewall/bot block in front of the login
    // server: the refresh token never got there and may still be valid.
    const loginGone = err.json ? (LOGIN_ERRORS.has(err.code) || res.status === 400 || res.status === 401)
      : (res.status === 400 || res.status === 401);
    if (loginGone) {
      throw new CareLinkAuthError(`${where} rejected (HTTP ${res.status} ${err.detail}); repeat the browser login`);
    }
    throw new Error(`${where} failed (HTTP ${res.status} ${err.detail})`);
  }
  const j = await res.json();
  if (!j.access_token) throw new Error('token refresh returned no access_token');
  const next = {
    ...tokens,
    access_token: j.access_token,
    refresh_token: j.refresh_token || tokens.refresh_token,
    expires_at: Number.isFinite(Number(j.expires_in)) ? now + Number(j.expires_in) * 1000 : undefined,
    refreshed_at: now,
  };
  if (!Number.isFinite(next.expires_at)) delete next.expires_at;
  await kv.put('tokens', JSON.stringify(next));
  return next;
}

/** Role, own username and the followed patient (env values win). */
export async function resolveIdentity(env, config, tokens, fetchImpl) {
  const get = async (path) => {
    const res = await fetchImpl(config.base_carelink + path, { headers: apiHeaders(tokens, false), signal: timeout() });
    if (res.status === 401 || res.status === 403) throw new CareLinkAuthError(`GET ${path} -> HTTP ${res.status}`);
    if (!res.ok) throw new Error(`GET ${path} -> HTTP ${res.status}`);
    return res.json();
  };
  const me = await get('/users/me');
  const role = String(me.role || '');
  const carePartner = /CARE_PARTNER/i.test(role);
  const payload = decodeJwtPayload(tokens.access_token) || {};
  let username = env.CARELINK_USERNAME
    || (payload.token_details && payload.token_details.preferred_username)
    || payload.preferred_username || null;
  if (!username) {
    const profile = await get('/users/me/profile');
    username = profile.username || null;
  }
  if (!username) throw new Error('could not determine the CareLink username (set CARELINK_USERNAME)');
  let patient = env.CARELINK_PATIENT_USERNAME || null;
  if (carePartner && !patient) {
    for (const path of ['/m2m/links/patients', '/links/patients']) {
      try {
        const list = await get(path);
        if (Array.isArray(list) && list.length && list[0].username) { patient = list[0].username; break; }
      } catch (e) {
        if (e instanceof CareLinkAuthError) throw e;
      }
    }
    if (!patient) throw new Error('no linked patient found (set CARELINK_PATIENT_USERNAME)');
  }
  return { role, care_partner: carePartner, username, patient };
}

async function fetchPatientData(config, tokens, fetchImpl) {
  const body = {
    username: config.identity.username,
    role: config.identity.care_partner ? 'carepartner' : 'patient',
    appVersion: APP_VERSION,
  };
  if (config.identity.care_partner) body.patientId = config.identity.patient;
  const res = await fetchImpl(`${config.base_cumulus}/display/message`, {
    method: 'POST', headers: apiHeaders(tokens, true), body: JSON.stringify(body), signal: timeout(),
  });
  if (res.status === 401 || res.status === 403) return { auth: true };
  if (!res.ok) throw new Error(`display/message -> HTTP ${res.status}`);
  const j = await res.json();
  if (!j || !j.patientData) throw new Error('display/message returned no patientData');
  return { patientData: j.patientData };
}

/**
 * One full poll: tokens -> config -> data -> snapshot. Writes KV only when needed.
 * Returns { snapshot, wrote, refreshed, patientData } (patientData stays in memory, for the uploader).
 */
export async function pollOnce(env, now, fetchImpl = fetch) {
  const kv = env.CARELINK;
  let tokens = await kv.get('tokens', { type: 'json' });
  if (!tokens || !tokens.refresh_token || !tokens.client_id) {
    throw new CareLinkAuthError('no CareLink tokens in KV (upload logindata.json as key "tokens")');
  }

  let config = await kv.get('config', { type: 'json' });
  const discoveryUrl = env.CARELINK_DISCOVERY_URL || DEFAULT_DISCOVERY_URL;
  let configDirty = false;
  if (!config || now - (config.at || 0) > CONFIG_TTL_MS || config.discovery_url !== discoveryUrl) {
    config = { ...(await discover(env, tokens, fetchImpl)), at: now, identity: null };
    configDirty = true;
  }

  let refreshed = false;
  const refresh = async () => {
    try {
      tokens = await refreshTokens(kv, config, tokens, now, fetchImpl);
    } catch (e) {
      // Another invocation may have rotated the token meanwhile: retry once with the stored one.
      const stored = await kv.get('tokens', { type: 'json' });
      if (e instanceof CareLinkAuthError && stored && stored.refresh_token !== tokens.refresh_token) {
        tokens = stored;
        tokens = await refreshTokens(kv, config, tokens, now, fetchImpl);
      } else {
        throw e;
      }
    }
    refreshed = true;
  };
  // A fresh logindata.json has no refreshed_at: count from when its access token was issued.
  const refreshedAt = tokens.refreshed_at || tokenIssuedAt(tokens) || now;
  if (tokenExpiresAt(tokens) - now < REFRESH_BEFORE_MS || now - refreshedAt > REFRESH_AT_LEAST_EVERY_MS) {
    await refresh();
  }

  if (!config.identity) {
    try {
      config.identity = await resolveIdentity(env, config, tokens, fetchImpl);
    } catch (e) {
      if (!(e instanceof CareLinkAuthError) || refreshed) throw e;
      await refresh();
      config.identity = await resolveIdentity(env, config, tokens, fetchImpl);
    }
    configDirty = true;
  }
  if (configDirty) await kv.put('config', JSON.stringify(config));

  let result = await fetchPatientData(config, tokens, fetchImpl);
  if (result.auth) {
    if (refreshed) throw new CareLinkAuthError('display/message rejected a fresh token');
    await refresh();
    result = await fetchPatientData(config, tokens, fetchImpl);
    if (result.auth) throw new CareLinkAuthError('display/message rejected the refreshed token');
  }

  const snapshot = buildSnapshot(result.patientData, now);
  const prev = await kv.get('snapshot', { type: 'json' });
  // Last detected set change (tubing fill) is carried from snapshot to snapshot.
  snapshot.set_change_at = detectSetChange(prev, snapshot, result.patientData) || (prev && prev.set_change_at) || null;
  snapshot.latches = updateLatches(prev && prev.latches, snapshot, now);
  const changed = !prev || snapshotFingerprint(prev) !== snapshotFingerprint(snapshot);
  const wrote = changed || now - (prev.fetched_at || 0) > SNAPSHOT_REWRITE_MS;
  if (wrote) await kv.put('snapshot', JSON.stringify(snapshot));
  return { snapshot, wrote, refreshed, patientData: result.patientData };
}

/** When is the next reading expected in CareLink? */
export function nextDue(snapshot, lagSec) {
  return snapshot && snapshot.last ? snapshot.last.t + 5 * MIN + lagSec * 1000 : null;
}

/** Should a request/cron trigger a CareLink fetch now? */
export function shouldFetch(snapshot, status, now, lastAttemptAt, lagSec) {
  if (status && status.next_retry_at && now < status.next_retry_at) return { fetch: false, reason: 'backoff after error' };
  const sinceAttempt = now - (lastAttemptAt || 0);
  if (!snapshot) return { fetch: sinceAttempt >= 20000, reason: 'no snapshot yet' };
  const sinceFetch = now - Math.max(snapshot.fetched_at || 0, lastAttemptAt || 0);
  const due = nextDue(snapshot, lagSec);
  if (due === null) return { fetch: sinceFetch >= 5 * MIN, reason: 'no glucose in snapshot' };
  if (now < due) return { fetch: sinceFetch >= 15 * MIN, reason: 'next reading not due yet' };
  const late = now - due;
  const gap = late < 5 * MIN ? 20000 : late < 15 * MIN ? 50000 : late < 60 * MIN ? 110000 : 290000;
  return { fetch: sinceFetch >= gap, reason: late < 1000 ? 'reading due' : `reading ${Math.round(late / 1000)} s late` };
}

/** Error bookkeeping with exponential backoff (1, 2, 4 … 30 min; auth errors 60 min). */
export function nextStatus(prev, error, now) {
  if (!error) return prev && prev.failures ? { failures: 0, last_ok_at: now } : null;
  const failures = ((prev && prev.failures) || 0) + 1;
  const auth = error instanceof CareLinkAuthError;
  const backoff = auth ? 60 * MIN : Math.min(30, 2 ** (failures - 1)) * MIN;
  return {
    failures,
    auth_error: auth,
    last_error: String(error.message || error).slice(0, 300),
    last_error_at: now,
    next_retry_at: now + backoff,
    last_ok_at: prev ? prev.last_ok_at || null : null,
  };
}
