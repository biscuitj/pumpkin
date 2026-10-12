import { test, beforeEach, afterEach } from 'node:test';
import assert from 'node:assert/strict';
import worker, { _resetCache } from '../src/index.js';

const MIN = 60000;
const realFetch = globalThis.fetch;
const realNow = Date.now;
const NOW = Date.parse('2026-10-09T08:10:00Z');

const env = {
  NS_URL: 'https://ns.example',
  NS_TOKEN: 'follower-x',
  CLOCK_TOKEN: 'clock-secret',
  IOB_ENABLED: 'true',
};

function day(latestAgeMin = 3) {
  const out = [];
  for (let i = 0; i < 288; i++) out.push({ sgv: 150 - (i === 0 ? 0 : 0), date: NOW - (latestAgeMin + i * 5) * MIN, direction: 'NOT COMPUTABLE' });
  return out;
}

let routes;
let calls;
beforeEach(() => {
  _resetCache();
  Date.now = () => NOW;
  calls = [];
  routes = {
    '/api/v1/entries/sgv.json': () => day(),
    '/api/v2/properties/iob': () => ({ iob: { iob: 1.234, mills: NOW - 4 * MIN, source: 'MiniMed', display: '1.23' } }),
  };
  globalThis.fetch = async (url) => {
    const u = new URL(url);
    calls.push(u);
    const handler = routes[u.pathname];
    if (!handler) return new Response('nope', { status: 404 });
    const body = handler(u);
    if (body instanceof Response) return body;
    return new Response(JSON.stringify(body), { status: 200 });
  };
});
afterEach(() => { globalThis.fetch = realFetch; Date.now = realNow; });

const req = (path, token = 'clock-secret') =>
  new Request(`https://bridge.example${path}`, { headers: token ? { authorization: `Bearer ${token}` } : {} });

test('rejects missing or wrong token, and fails closed without CLOCK_TOKEN', async () => {
  assert.equal((await worker.fetch(req('/clock', null), env)).status, 401);
  assert.equal((await worker.fetch(req('/clock', 'wrong'), env)).status, 401);
  assert.equal((await worker.fetch(req('/clock'), { ...env, CLOCK_TOKEN: '' })).status, 401);
  assert.equal((await worker.fetch(req('/health', null), env)).status, 200);
});

test('returns SugarClock custom format with TIR', async () => {
  const res = await worker.fetch(req('/clock'), env);
  assert.equal(res.status, 200);
  const body = await res.json();
  assert.equal(body.glucose, 150);
  assert.equal(body.trend, 'Flat');
  assert.equal(body.force_mode, -1);
  assert.equal(body.tir, 100);
  assert.equal(body.message, '');
  assert.equal(body.tir_below, 0);
  assert.equal(body.tir_above, 0);
  assert.equal(body.iob, 1.23);
  assert.ok(Number.isInteger(body.timestamp));
  // Nightscout token is passed along, never the clock token
  assert.ok(calls.every((u) => u.searchParams.get('token') === 'follower-x'));
});

test('stale data returns 503 instead of an old value', async () => {
  routes['/api/v1/entries/sgv.json'] = () => day(45);
  const res = await worker.fetch(req('/clock'), env);
  assert.equal(res.status, 503);
  assert.equal((await res.json()).error, 'stale');
});

test('Nightscout failure returns 502', async () => {
  routes['/api/v1/entries/sgv.json'] = () => new Response('down', { status: 500 });
  assert.equal((await worker.fetch(req('/clock'), env)).status, 502);
});

test('TIR failure does not break the glucose response', async () => {
  routes['/api/v1/entries/sgv.json'] = (u) =>
    (u.searchParams.has('find[date][$gte]') ? new Response('slow', { status: 504 }) : day().slice(0, 12));
  const res = await worker.fetch(req('/clock'), env);
  assert.equal(res.status, 200);
  const body = await res.json();
  assert.equal(body.glucose, 150);
  assert.equal(body.tir, -1);
});

test('debug endpoint explains decisions', async () => {
  const body = await (await worker.fetch(req('/debug'), env)).json();
  assert.equal(body.trend.source, 'slope');
  assert.equal(body.tir.coverage, 1);
  assert.equal(body.reading.age_min, 3);
});

test('IOB failure or stale IOB gives null without breaking glucose', async () => {
  routes['/api/v2/properties/iob'] = () => new Response('err', { status: 500 });
  let body = await (await worker.fetch(req('/clock'), env)).json();
  assert.equal(body.glucose, 150);
  assert.equal(body.iob, null);

  routes['/api/v2/properties/iob'] = () => ({ iob: { iob: 2.5, mills: NOW - 40 * MIN } });
  body = await (await worker.fetch(req('/clock'), env)).json();
  assert.equal(body.iob, null);

  routes['/api/v2/properties/iob'] = () => ({ iob: { iob: 0, mills: NOW - MIN } });
  body = await (await worker.fetch(req('/clock'), env)).json();
  assert.equal(body.iob, 0);
});

test('IOB is off unless explicitly enabled', async () => {
  const { IOB_ENABLED, ...noIob } = env;
  const body = await (await worker.fetch(req('/clock'), noIob)).json();
  assert.equal(body.iob, null);
  assert.ok(calls.every((u) => u.pathname !== '/api/v2/properties/iob'));
});

test('next_poll_sec is returned (default lag without ObjectIds)', async () => {
  // newest reading 3 min old -> next at +5 min + 86 s + 5 s = 213 s from now
  const body = await (await worker.fetch(req('/clock'), env)).json();
  assert.equal(body.next_poll_sec, 211);
});
