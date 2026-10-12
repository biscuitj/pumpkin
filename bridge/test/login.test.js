import { test } from 'node:test';
import assert from 'node:assert/strict';
import { authorizeUrl, codeFromRedirect, exchangeCode, loginConfigFrom, regionFor, resolveLoginConfig } from '../tools/carelink-login-lib.mjs';

const DISCOVERY = {
  supportedCountries: [{ BE: { region: 'EU' } }, { US: { region: 'US' } }],
  CP: [
    { region: 'EU', UseSSOConfiguration: 'SSOConfigurationAuth0', SSOConfigurationAuth0: 'https://sso.example/eu.json', SSOConfiguration: 'https://sso.example/eu-old.json' },
    { region: 'US', UseSSOConfiguration: 'SSOConfiguration', SSOConfiguration: 'https://sso.example/us.json' },
  ],
};
const SSO = {
  server: { hostname: 'auth.example', port: 443, prefix: '' },
  client: { client_id: 'abc', scope: 'openid profile offline_access', redirect_uri: 'com.example.app:/sso', audience: 'https://api.example' },
  system_endpoints: { authorization_endpoint_path: '/authorize', token_endpoint_path: '/oauth/token' },
};

function fakeFetch(routes) {
  const calls = [];
  const impl = async (url, init = {}) => {
    calls.push({ url, init });
    const r = routes[url];
    if (!r) return new Response('not found', { status: 404 });
    return typeof r === 'function' ? r(init) : new Response(JSON.stringify(r), { status: 200 });
  };
  impl.calls = calls;
  return impl;
}

test('region and login endpoints from discovery + Auth0 SSO config', async () => {
  assert.equal(regionFor(DISCOVERY, 'be'), 'EU');
  assert.equal(regionFor(DISCOVERY, 'XX'), null);
  const fetchImpl = fakeFetch({ 'https://disc.example': DISCOVERY, 'https://sso.example/eu.json': SSO });
  const cfg = await resolveLoginConfig({ discoveryUrl: 'https://disc.example', country: 'BE', fetchImpl });
  assert.deepEqual(cfg, {
    region: 'EU', clientId: 'abc', redirectUri: 'com.example.app:/sso', scope: 'openid profile offline_access',
    audience: 'https://api.example', authorizeUrl: 'https://auth.example:443/authorize', tokenUrl: 'https://auth.example:443/oauth/token',
  });
  const url = new URL(authorizeUrl(cfg));
  assert.equal(url.origin + url.pathname, 'https://auth.example/authorize');
  assert.equal(url.searchParams.get('response_type'), 'code');
  assert.equal(url.searchParams.get('redirect_uri'), 'com.example.app:/sso');
  assert.equal(url.searchParams.get('audience'), 'https://api.example');
});

test('a non-Auth0 region or an incomplete SSO config is refused clearly', () => {
  assert.throws(() => loginConfigFrom(DISCOVERY, SSO, 'US'), /non-Auth0/);
  assert.throws(() => loginConfigFrom(DISCOVERY, { ...SSO, client: { client_id: 'abc' } }, 'EU'), /client.redirect_uri/);
  assert.throws(() => loginConfigFrom(DISCOVERY, SSO, 'AP'), /no configuration for region AP/);
});

test('the code comes only from the redirect to the app scheme', () => {
  const r = 'com.example.app:/sso';
  assert.equal(codeFromRedirect('com.example.app:/sso?code=XyZ123&state=s', r), 'XyZ123');
  assert.equal(codeFromRedirect('com.example.app:/sso?state=s&code=a%2Fb', r), 'a/b');
  assert.equal(codeFromRedirect('https://auth.example/login?code=nope', r), null);
  assert.equal(codeFromRedirect('com.example.app:/sso?error=access_denied', r), null);
  assert.equal(codeFromRedirect(undefined, r), null);
});

test('token exchange: form post, bridge format, clear errors', async () => {
  const cfg = loginConfigFrom(DISCOVERY, SSO, 'EU');
  let body;
  const ok = fakeFetch({ [cfg.tokenUrl]: (init) => { body = new URLSearchParams(init.body); return new Response(JSON.stringify({ access_token: 'a.b.c', refresh_token: 'r1', scope: 'openid', expires_in: 3600, token_type: 'Bearer' }), { status: 200 }); } });
  const tokens = await exchangeCode(cfg, 'CODE', ok);
  assert.equal(body.get('grant_type'), 'authorization_code');
  assert.equal(body.get('code'), 'CODE');
  assert.equal(body.get('client_id'), 'abc');
  assert.equal(body.get('redirect_uri'), 'com.example.app:/sso');
  assert.deepEqual(tokens, { access_token: 'a.b.c', refresh_token: 'r1', scope: 'openid', client_id: 'abc' });

  const bad = fakeFetch({ [cfg.tokenUrl]: () => new Response(JSON.stringify({ error: 'invalid_grant', error_description: 'expired code' }), { status: 403 }) });
  await assert.rejects(exchangeCode(cfg, 'OLD', bad), /HTTP 403\): invalid_grant: expired code/);
});
