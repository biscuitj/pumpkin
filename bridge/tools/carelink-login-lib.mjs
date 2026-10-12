// Pure parts of the CareLink care-partner login (no browser, no Wrangler), so
// they can be unit-tested. Written for Pumpkin from the public CareLink
// discovery and Auth0 SSO configuration; the login itself is a standard OAuth
// authorization-code flow whose redirect goes to the CareLink app's URL scheme.

import { DEFAULT_DISCOVERY_URL, USER_AGENT } from '../src/carelink.js';

export { DEFAULT_DISCOVERY_URL };

async function getJson(url, fetchImpl) {
  const res = await fetchImpl(url, { headers: { Accept: 'application/json', 'User-Agent': USER_AGENT } });
  if (!res.ok) throw new Error(`GET ${url} -> HTTP ${res.status}`);
  return res.json();
}

/** Region for a country code from the discovery document ("BE" -> "EU"). */
export function regionFor(discovery, country) {
  const cc = String(country || '').toUpperCase();
  for (const c of discovery.supportedCountries || []) {
    if (c && c[cc] && c[cc].region) return String(c[cc].region).toUpperCase();
  }
  return null;
}

/**
 * Login endpoints for a region: authorize URL parameters and token URL.
 * Only the Auth0 configuration (current EU and US logins) is supported.
 */
export function loginConfigFrom(discovery, sso, region) {
  const cp = (discovery.CP || []).find((c) => String(c.region || '').toUpperCase() === region);
  if (!cp) throw new Error(`CareLink discovery has no configuration for region ${region}`);
  const key = String(cp.UseSSOConfiguration || '');
  if (!/auth0/i.test(key)) {
    throw new Error(`CareLink uses a non-Auth0 login for region ${region} (${key || 'unknown'}); this tool only supports Auth0`);
  }
  const s = sso.server || {};
  const client = sso.client || {};
  const paths = sso.system_endpoints || {};
  for (const [name, value] of Object.entries({
    'server.hostname': s.hostname, 'client.client_id': client.client_id, 'client.redirect_uri': client.redirect_uri,
    'system_endpoints.authorization_endpoint_path': paths.authorization_endpoint_path,
    'system_endpoints.token_endpoint_path': paths.token_endpoint_path,
  })) {
    if (!value) throw new Error(`CareLink SSO configuration is missing ${name}`);
  }
  let base = `https://${s.hostname}${s.port ? `:${s.port}` : ''}/${s.prefix || ''}`;
  if (base.endsWith('/')) base = base.slice(0, -1);
  return {
    region,
    clientId: client.client_id,
    redirectUri: client.redirect_uri,
    scope: client.scope || '',
    audience: client.audience || '',
    authorizeUrl: base + paths.authorization_endpoint_path,
    tokenUrl: base + paths.token_endpoint_path,
  };
}

/** Discovery -> SSO configuration -> login config for a country (default BE). */
export async function resolveLoginConfig({ discoveryUrl = DEFAULT_DISCOVERY_URL, country = 'BE', region = null, fetchImpl = fetch } = {}) {
  const discovery = await getJson(discoveryUrl, fetchImpl);
  const reg = (region && String(region).toUpperCase()) || regionFor(discovery, country) || 'EU';
  const cp = (discovery.CP || []).find((c) => String(c.region || '').toUpperCase() === reg);
  if (!cp) throw new Error(`CareLink discovery has no configuration for region ${reg}`);
  const ssoUrl = cp[cp.UseSSOConfiguration] || cp.SSOConfiguration;
  if (!ssoUrl) throw new Error('CareLink discovery has no SSO configuration URL');
  const sso = await getJson(ssoUrl, fetchImpl);
  return loginConfigFrom(discovery, sso, reg);
}

/** The URL the browser opens to log in. */
export function authorizeUrl(cfg) {
  const params = new URLSearchParams({ client_id: cfg.clientId, response_type: 'code', redirect_uri: cfg.redirectUri });
  if (cfg.scope) params.set('scope', cfg.scope);
  if (cfg.audience) params.set('audience', cfg.audience);
  return `${cfg.authorizeUrl}?${params.toString()}`;
}

/** The authorization code from a redirect to the app's URL scheme, or null. */
export function codeFromRedirect(url, redirectUri) {
  if (!url || !redirectUri || !String(url).startsWith(redirectUri)) return null;
  const query = String(url).split('?')[1] || '';
  const code = new URLSearchParams(query.split('#')[0]).get('code');
  return code || null;
}

/** Exchange the code for tokens; returns what the bridge stores as KV key "tokens". */
export async function exchangeCode(cfg, code, fetchImpl = fetch) {
  const body = new URLSearchParams({
    grant_type: 'authorization_code',
    client_id: cfg.clientId,
    code,
    redirect_uri: cfg.redirectUri,
  });
  const res = await fetchImpl(cfg.tokenUrl, {
    method: 'POST',
    headers: { 'Content-Type': 'application/x-www-form-urlencoded', Accept: 'application/json', 'User-Agent': USER_AGENT },
    body: body.toString(),
  });
  const text = await res.text();
  let json = null;
  try { json = JSON.parse(text); } catch { /* reported below */ }
  if (!res.ok || !json || !json.access_token || !json.refresh_token) {
    const detail = json && (json.error_description || json.error) ? `${json.error}: ${json.error_description || ''}` : text.slice(0, 200);
    throw new Error(`token exchange failed (HTTP ${res.status}): ${detail}`);
  }
  const tokens = { ...json, client_id: cfg.clientId };
  delete tokens.expires_in;
  delete tokens.token_type;
  return tokens;
}
