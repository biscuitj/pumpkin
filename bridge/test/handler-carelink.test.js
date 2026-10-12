import { test, beforeEach, afterEach } from 'node:test';
import assert from 'node:assert/strict';
import worker, { _resetCache, clockWarnings } from '../src/index.js';
import { buildSnapshot } from '../src/carelink-data.js';
import { NOW, MIN, patientData, memoryKV, jwt, fakeCareLink } from './fixtures.js';

const realFetch = globalThis.fetch;
const realNow = Date.now;

const TOKENS = () => ({
  access_token: jwt(NOW + 30 * MIN), refresh_token: 'rt-0', client_id: 'client', client_secret: 'secret',
  'mag-identifier': 'mag-1', refreshed_at: NOW - 60 * MIN,
});

function nsDay(latestAgeMin = 3) {
  const out = [];
  for (let i = 0; i < 288; i++) out.push({ sgv: 150, date: NOW - (latestAgeMin + i * 5) * MIN, direction: 'NOT COMPUTABLE' });
  return out;
}

let cl;
let nsCalls;
let nsDown;
function install(opts) {
  cl = fakeCareLink(opts);
  globalThis.fetch = async (url, init) => {
    const u = new URL(url);
    if (u.hostname === 'ns.example') {
      nsCalls.push(u);
      if (nsDown) return new Response('down', { status: 500 });
      if (u.pathname === '/api/v1/entries/sgv.json') return new Response(JSON.stringify(nsDay()), { status: 200 });
      return new Response('nope', { status: 404 });
    }
    return cl.handler(url, init);
  };
}

beforeEach(() => {
  _resetCache();
  Date.now = () => NOW;
  nsCalls = [];
  nsDown = false;
  install();
});
afterEach(() => { globalThis.fetch = realFetch; Date.now = realNow; });

const envWith = (kv, extra = {}) => ({
  NS_URL: 'https://ns.example', NS_TOKEN: 'follower-x', CLOCK_TOKEN: 'clock-secret', CARELINK: kv, ...extra,
});
const req = (path, method = 'GET') =>
  new Request(`https://bridge.example${path}`, { method, headers: { authorization: 'Bearer clock-secret' } });
const clock = async (env) => {
  const res = await worker.fetch(req('/clock'), env);
  return { status: res.status, body: await res.json() };
};

test('fresh CareLink snapshot feeds /clock with pump extras, no fetch needed', async () => {
  const pd = patientData({
    trend: 'UP_DOUBLE',
    extra: {
      pumpBannerState: [{ type: 'TEMP_TARGET', timeRemaining: 95 }],
      notificationHistory: {
        activeNotifications: [
          { faultId: 802, dateTime: '2026-10-09T13:40:00.000Z' },
          { faultId: 105, dateTime: '2026-10-09T13:30:00.000Z' },
          { faultId: 140, dateTime: '2026-10-09T13:50:00.000Z', alertSilenced: true },
        ],
        clearedNotifications: [],
      },
    },
  });
  const kv = memoryKV({ tokens: TOKENS(), snapshot: buildSnapshot(pd, NOW - 2 * MIN) });
  const { status, body } = await clock(envWith(kv));
  assert.equal(status, 200);
  assert.equal(body.source, 'carelink');
  assert.equal(body.glucose, 120);
  assert.equal(body.trend, 'DoubleUp');
  assert.equal(body.trend_arrows, 2);
  assert.equal(body.timestamp, Math.floor((NOW - 3 * MIN) / 1000));
  assert.equal(body.iob, 1.35);
  assert.equal(body.tir, 100);
  assert.equal(body.temp_target, true);
  assert.equal(body.temp_target_min, 95);
  assert.equal(body.alert, 'CONNECT SET'); // English by default
  assert.deepEqual(body.alerts.map((a) => a.short), ['CONNECT SET', 'RESERVOIR LOW']);
  const nl = await clock({ ...envWith(kv), LANG: 'nl' });
  assert.equal(nl.body.alert, 'SET AANSLUITEN'); // LANG = nl
  assert.deepEqual(body.alerts.map((a) => a.category), ['set_connect', 'reservoir_low']); // glucose alert left out
  assert.equal(body.alerts[0].silenced, true);
  assert.deepEqual(body.warnings, ['warning', 'reservoir']); // set connect -> triangle, reservoir low -> icon
  assert.equal(body.reservoir_units, 42.5);
  assert.equal(body.pump_battery, 75);
  assert.equal(body.phone_battery, 88);
  assert.equal(body.sensor_hours_left, 31);
  assert.equal(body.bridge_alert, '');
  assert.ok(body.next_poll_sec >= 10 && body.next_poll_sec <= 300);
  // reading at -3 min, next due at +2 min + 75 s lag + 5 s margin
  assert.equal(body.next_poll_sec, 200);
  assert.equal(cl.calls.length, 0, 'not due yet: no CareLink call');
  assert.equal(nsCalls.length, 0, 'CareLink TIR is complete: no Nightscout call');
});

test('due reading triggers a CareLink fetch on /clock and stores the snapshot', async () => {
  const kv = memoryKV({ tokens: TOKENS(), snapshot: buildSnapshot(patientData({ lastAgeMin: 8 }), NOW - 6 * MIN) });
  const { status, body } = await clock(envWith(kv));
  assert.equal(status, 200);
  assert.equal(body.source, 'carelink');
  assert.equal(body.timestamp, Math.floor((NOW - 3 * MIN) / 1000));
  assert.ok(cl.calls.some((c) => c.url.pathname.endsWith('/display/message')));
  assert.ok(kv.puts.includes('snapshot'));
});

test('no tokens: Nightscout fallback, bridge_alert asks for a login', async () => {
  const kv = memoryKV({});
  const { status, body } = await clock(envWith(kv));
  assert.equal(status, 200);
  assert.equal(body.source, 'nightscout');
  assert.equal(body.glucose, 150);
  assert.equal(body.bridge_alert, 'CARELINK LOGIN NEEDED');
  assert.equal(body.iob, null);
  assert.equal(body.alert, '');
  assert.deepEqual(body.alerts, []);
  assert.equal(body.temp_target, false);
  const status2 = JSON.parse(kv.store.get('status'));
  assert.equal(status2.auth_error, true);
  const nl = await clock({ ...envWith(kv), LANG: 'nl' });
  assert.equal(nl.body.bridge_alert, 'CARELINK LOGIN NODIG');
});

test('rejected refresh token: fallback plus login alert, then backoff (no hammering)', async () => {
  install({ refreshStatus: 401 });
  const kv = memoryKV({ tokens: { ...TOKENS(), access_token: jwt(NOW - MIN) } });
  let r = await clock(envWith(kv));
  assert.equal(r.body.source, 'nightscout');
  assert.equal(r.body.bridge_alert, 'CARELINK LOGIN NEEDED');
  const before = cl.calls.length;
  _resetCache(); // new isolate, only KV status remains
  r = await clock(envWith(kv));
  assert.equal(cl.calls.length, before, 'backoff: no second login attempt right away');
  assert.equal(r.body.bridge_alert, 'CARELINK LOGIN NEEDED');
});

test('stale CareLink snapshot and Nightscout down: 502, never an old value', async () => {
  install({ displayStatuses: [500, 500, 500] });
  nsDown = true;
  const kv = memoryKV({ tokens: TOKENS(), snapshot: buildSnapshot(patientData({ lastAgeMin: 50 }), NOW - 45 * MIN) });
  const { status } = await clock(envWith(kv));
  assert.equal(status, 502);
});

test('old pump data: glucose still served, pump extras dropped', async () => {
  // readings fresh, but the pump fields are 20 min old (device_data_at)
  const pd = patientData({ extra: { lastMedicalDeviceDataUpdateServerTime: NOW - 20 * MIN, lastConduitUpdateServerDateTime: NOW - 20 * MIN } });
  const kv = memoryKV({ tokens: TOKENS(), snapshot: buildSnapshot(pd, NOW - MIN) });
  const { body } = await clock(envWith(kv));
  assert.equal(body.source, 'carelink');
  assert.equal(body.glucose, 120);
  assert.equal(body.iob, null);
  assert.equal(body.reservoir_units, null);
  assert.deepEqual(body.warnings, []);
});

test('CARELINK_ENABLED=false ignores KV entirely', async () => {
  const kv = memoryKV({ tokens: TOKENS(), snapshot: buildSnapshot(patientData(), NOW) });
  const { body } = await clock(envWith(kv, { CARELINK_ENABLED: 'false' }));
  assert.equal(body.source, 'nightscout');
  assert.equal(body.bridge_alert, '');
});

test('POST /carelink/poll forces a fetch and /debug shows CareLink state', async () => {
  const kv = memoryKV({ tokens: TOKENS(), snapshot: buildSnapshot(patientData(), NOW - MIN) });
  const env = envWith(kv);
  assert.equal((await worker.fetch(req('/carelink/poll'), env)).status, 405);
  const res = await worker.fetch(req('/carelink/poll', 'POST'), env);
  assert.equal(res.status, 200);
  const dbg = await res.json();
  assert.equal(dbg.carelink.decision.reason, 'forced');
  assert.ok(cl.calls.some((c) => c.url.pathname.endsWith('/display/message')));
  assert.equal(dbg.carelink.snapshot.last.sg, 120);
  assert.equal(dbg.clock.source, 'carelink');
});

test('cron keepalive polls CareLink when due', async () => {
  const kv = memoryKV({ tokens: TOKENS() });
  const waits = [];
  await worker.scheduled({}, envWith(kv), { waitUntil: (p) => waits.push(p) });
  await Promise.all(waits);
  assert.ok(kv.store.has('snapshot'));
});

test('clock warnings: categories map to icons/texts, deduped, priority order, glucose/info ignored', () => {
  const a = (category) => ({ category });
  assert.deepEqual(clockWarnings([a('delivery'), a('battery_now'), a('reservoir_low'), a('sensor_change'), a('set_change')]),
    ['warning', 'battery', 'reservoir', 'sensor', 'set']);
  assert.deepEqual(clockWarnings([a('battery_now'), a('battery_low'), a('glucose'), a('info'), a('other'), a('bg_required')]),
    ['battery', 'bg']);
  assert.deepEqual(clockWarnings([a('calibration'), a('sensor_signal'), a('set_connect')]), ['warning']);
  assert.deepEqual(clockWarnings([]), []);
});

test('without Nightscout: CareLink alone serves the clock, and nothing calls Nightscout', async () => {
  const pd = patientData();
  const kv = memoryKV({ tokens: TOKENS(), snapshot: buildSnapshot(pd, NOW - 2 * MIN) });
  const { status, body } = await clock({ CLOCK_TOKEN: 'clock-secret', CARELINK: kv });
  assert.equal(status, 200);
  assert.equal(body.source, 'carelink');
  assert.equal(body.glucose, 120);
  assert.equal(nsCalls.length, 0);
});
