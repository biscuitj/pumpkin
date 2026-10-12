import { test, beforeEach, afterEach } from 'node:test';
import assert from 'node:assert/strict';
import { buildSnapshot } from '../src/carelink-data.js';
import {
  treatmentsFromMarkers, entriesFrom, deviceStatusFrom, tempTargetTreatment, uploadToNightscout, uploadEnabled, DEVICE,
  RAW_DEVICE,
} from '../src/nightscout-upload.js';
import worker, { _resetCache } from '../src/index.js';
import { NOW, MIN, patientData, memoryKV, jwt, fakeCareLink } from './fixtures.js';

const isoAt = (minAgo) => new Date(NOW - minAgo * MIN).toISOString();

/** Markers in both shapes CareLink has used: flat, and under data.dataValues. */
const MARKERS = [
  { type: 'INSULIN', dateTime: isoAt(50), activationType: 'AUTOCORRECTION', deliveredFastAmount: 0.4, programmedFastAmount: 0.4, completed: true },
  { type: 'MEAL', timestamp: isoAt(30), data: { dataValues: { amount: '45' } } },
  { type: 'INSULIN', timestamp: isoAt(29.5), data: { dataValues: { activationType: 'RECOMMENDED', deliveredFastAmount: '3.25', programmedFastAmount: '3.25' } } },
  { type: 'MEAL', dateTime: isoAt(10), amount: 12 },
  { type: 'BG_READING', dateTime: isoAt(8), value: 134 },
  { type: 'INSULIN', dateTime: isoAt(5), activationType: 'MANUAL', deliveredFastAmount: 0, programmedFastAmount: 0 },
  { type: 'AUTO_BASAL_DELIVERY', dateTime: isoAt(4), bolusAmount: 0.05 },
];

function fakeNightscout({ entries = [], devicestatus = [], treatments = [], failPost = false } = {}) {
  const db = { entries: [...entries], devicestatus: [...devicestatus], treatments: [...treatments] };
  const calls = [];
  const reply = (body, status = 200) => new Response(JSON.stringify(body), { status });
  const handler = async (url, init = {}) => {
    const u = new URL(url);
    const method = init.method || 'GET';
    calls.push({ method, path: u.pathname, url: u, headers: init.headers || {} });
    const coll = u.pathname.match(/\/api\/v1\/(entries|devicestatus|treatments)/)?.[1];
    if (!coll) return reply({ error: 'nope' }, 404);
    if (method === 'POST') {
      if (failPost) return reply({ error: 'boom' }, 500);
      const docs = [].concat(JSON.parse(init.body));
      for (const d of docs) {
        const key = coll === 'entries' ? (x) => x.date === d.date && x.type === d.type
          : coll === 'treatments' ? (x) => x.created_at === d.created_at && x.eventType === d.eventType
            : () => false;
        const i = db[coll].findIndex(key);
        if (i >= 0) db[coll][i] = d; else db[coll].push(d);
      }
      return reply(docs);
    }
    let rows = db[coll].slice();
    for (const [k, v] of u.searchParams) {
      const m = k.match(/^find\[(\w+)\]$/);
      if (m) rows = rows.filter((r) => String(r[m[1]]) === v);
    }
    const t = (r) => (coll === 'entries' ? r.date : Date.parse(r.created_at));
    rows.sort((a, b) => t(b) - t(a));
    return reply(rows.slice(0, Number(u.searchParams.get('count') || 10)));
  };
  return { handler, calls, db, posts: () => calls.filter((c) => c.method === 'POST') };
}

const pdWith = (extra = {}) => patientData({ trend: 'UP', extra: { markers: MARKERS, ...extra } });

// ---------------------------------------------------------------- mapping

test('markers -> treatments: auto correction, meal bolus merged, lone carbs, finger BG', () => {
  const t = treatmentsFromMarkers(pdWith(), NOW - 60 * MIN);
  assert.deepEqual(t.map((x) => [x.eventType, x.insulin ?? null, x.carbs ?? null, x.glucose ?? null]), [
    ['Correction Bolus', 0.4, null, null],
    ['Meal Bolus', 3.25, 45, null],
    ['Carb Correction', null, 12, null],
    ['BG Check', null, null, 134],
  ]);
  assert.equal(t[0].notes, 'Auto-correctie');
  assert.ok(t.every((x) => x.enteredBy === DEVICE));
  assert.equal(t[1].created_at, isoAt(29.5));
  // since filter
  assert.deepEqual(treatmentsFromMarkers(pdWith(), NOW - 20 * MIN).map((x) => x.eventType), ['Carb Correction', 'BG Check']);
  assert.deepEqual(treatmentsFromMarkers(patientData(), 0), []);
});

test('entries: only newer than Nightscout, pump trend on the newest', () => {
  const snap = buildSnapshot(pdWith(), NOW);
  const e = entriesFrom(snap, NOW - 14 * MIN);
  assert.equal(e.length, 3); // -13, -8, -3
  assert.deepEqual(e.map((x) => x.direction), ['NONE', 'NONE', 'SingleUp']);
  assert.equal(e[2].date, NOW - 3 * MIN);
  assert.equal(e[2].dateString, isoAt(3));
  assert.equal(e[2].type, 'sgv');
  assert.equal(entriesFrom(snap, NOW).length, 0);
});

test('devicestatus carries pump IOB where Nightscout looks for it, plus alerts', () => {
  const snap = buildSnapshot(pdWith({
    pumpBannerState: [{ type: 'TEMP_TARGET', timeRemaining: 60 }],
    notificationHistory: { activeNotifications: [{ faultId: 109, dateTime: isoAt(20) }], clearedNotifications: [] },
  }), NOW);
  const d = deviceStatusFrom(snap);
  assert.equal(d.device, DEVICE);
  assert.equal(d.created_at, new Date(NOW - 20000).toISOString());
  assert.equal(d.pump.iob.bolusiob, 1.35);
  assert.equal(d.pump.iob.iob, 1.35);
  assert.equal(d.pump.reservoir, 42.5);
  assert.deepEqual(d.pump.battery, { percent: 75 });
  assert.equal(d.pump.status.status, 'normal');
  assert.equal(d.uploader.battery, 88);
  assert.equal(d.carelink.alerts[0].short, 'SET VERVANGEN');
  assert.deepEqual(d.carelink.temp_target, { active: true, minutes_left: 60 });
});

test('temp target: start, keep, restart on change, cancel when stopped early', () => {
  const on = buildSnapshot(patientData({ extra: { pumpBannerState: [{ type: 'TEMP_TARGET', timeRemaining: 120 }] } }), NOW);
  const off = buildSnapshot(patientData(), NOW);
  const start = tempTargetTreatment(on, null, NOW);
  assert.equal(start.eventType, 'Temporary Target');
  assert.equal(start.duration, 120);
  assert.equal(start.targetTop, 150);
  // already in Nightscout, started 30 min ago for 150 min -> same end, nothing to do
  assert.equal(tempTargetTreatment(on, { created_at: isoAt(30), duration: 150 }, NOW), null);
  // extended on the pump -> new one
  assert.equal(tempTargetTreatment(on, { created_at: isoAt(30), duration: 60 }, NOW).duration, 120);
  // stopped on the pump while Nightscout still shows it -> cancel (duration 0)
  assert.equal(tempTargetTreatment(off, { created_at: isoAt(30), duration: 150 }, NOW).duration, 0);
  // nothing active anywhere
  assert.equal(tempTargetTreatment(off, { created_at: isoAt(300), duration: 60 }, NOW), null);
  assert.equal(tempTargetTreatment(off, null, NOW), null);
});

test('upload is enabled only with a secret or upload token', () => {
  assert.equal(uploadEnabled({ NS_URL: 'https://ns.example' }), false);
  assert.equal(uploadEnabled({ NS_URL: 'https://ns.example', NS_API_SECRET: 's' }), true);
  assert.equal(uploadEnabled({ NS_URL: 'https://ns.example', NS_UPLOAD_TOKEN: 't' }), true);
  assert.equal(uploadEnabled({ NS_URL: 'https://ns.example', NS_API_SECRET: 's', NS_UPLOAD: 'false' }), false);
});

// ---------------------------------------------------------------- upload

const nsEnv = { NS_URL: 'https://ns.example', NS_API_SECRET: 'secret', NS_TOKEN: 'follower-x' };
const withKv = () => ({ ...nsEnv, CARELINK: memoryKV() });

test('first upload: gap since Nightscout newest, devicestatus, recent treatments only', async () => {
  const ns = fakeNightscout({ entries: [{ type: 'sgv', sgv: 110, date: NOW - 23 * MIN }] });
  const pd = pdWith();
  const snapshot = buildSnapshot(pd, NOW);
  const r = await uploadToNightscout(nsEnv, { snapshot, patientData: pd }, NOW, ns.handler);
  assert.deepEqual(r, { entries: 4, devicestatus: true, treatments: 2, temp_target: null });
  // first run: no bridge treatments yet -> only the last 15 min, not ns10be's history
  assert.deepEqual(ns.db.treatments.map((t) => t.eventType), ['Carb Correction', 'BG Check']);
  // API secret goes as sha1 header; the follower token is never used to write
  assert.ok(ns.calls.every((c) => c.headers['api-secret'] === 'e5e9fa1ba31ecd1ae84f75caaa474f3a663f05f4'));
  assert.ok(ns.calls.every((c) => !c.url.searchParams.has('token')));
});

test('second upload with nothing new posts nothing; later markers are picked up', async () => {
  const ns = fakeNightscout();
  const env = withKv();
  const pd = pdWith();
  const snapshot = buildSnapshot(pd, NOW);
  await uploadToNightscout(env, { snapshot, patientData: pd }, NOW, ns.handler);
  const posted = ns.posts().length;
  const r = await uploadToNightscout(env, { snapshot, patientData: pd }, NOW + MIN, ns.handler);
  assert.deepEqual(r, { entries: 0, devicestatus: false, treatments: 0, temp_target: null });
  assert.ok(!ns.db.treatments.some((t) => t.eventType === 'Meal Bolus'), 'nothing from before the first upload');
  assert.equal(env.CARELINK.puts.filter((k) => k === 'ns_upload_since').length, 1);
  assert.equal(ns.posts().length, posted);

  // a correction bolus marker that showed up late (older than the newest uploaded one)
  const late = { type: 'INSULIN', dateTime: isoAt(12), activationType: 'AUTOCORRECTION', deliveredFastAmount: 0.6 };
  const pd2 = pdWith({ markers: [...MARKERS, late] });
  const r2 = await uploadToNightscout(env, { snapshot, patientData: pd2 }, NOW + 2 * MIN, ns.handler);
  assert.equal(r2.treatments, 1);
  assert.equal(ns.db.treatments.filter((t) => t.eventType === 'Correction Bolus').length, 1);
});

test('upload token instead of API secret goes in the query', async () => {
  const ns = fakeNightscout();
  const pd = patientData();
  await uploadToNightscout({ NS_URL: 'https://ns.example', NS_UPLOAD_TOKEN: 'admin-tok' }, { snapshot: buildSnapshot(pd, NOW), patientData: pd }, NOW, ns.handler);
  assert.ok(ns.calls.every((c) => c.url.searchParams.get('token') === 'admin-tok' && !c.headers['api-secret']));
});

test('temp target is posted once and cancelled once', async () => {
  const ns = fakeNightscout();
  const on = patientData({ extra: { pumpBannerState: [{ type: 'TEMP_TARGET', timeRemaining: 90 }] } });
  let r = await uploadToNightscout(nsEnv, { snapshot: buildSnapshot(on, NOW), patientData: on }, NOW, ns.handler);
  assert.equal(r.temp_target, 'start');
  r = await uploadToNightscout(nsEnv, { snapshot: buildSnapshot(on, NOW + 5 * MIN), patientData: on }, NOW + 5 * MIN, ns.handler);
  // 5 min later the pump says 90 left again (fixture), Nightscout says 85: within tolerance
  assert.equal(r.temp_target, null);
  const off = patientData();
  r = await uploadToNightscout(nsEnv, { snapshot: buildSnapshot(off, NOW + 10 * MIN), patientData: off }, NOW + 10 * MIN, ns.handler);
  assert.equal(r.temp_target, 'cancel');
  r = await uploadToNightscout(nsEnv, { snapshot: buildSnapshot(off, NOW + 15 * MIN), patientData: off }, NOW + 15 * MIN, ns.handler);
  assert.equal(r.temp_target, null);
});

// ---------------------------------------------------------------- through the Worker

const realFetch = globalThis.fetch;
const realNow = Date.now;
let ns;
let cl;
beforeEach(() => {
  _resetCache();
  Date.now = () => NOW;
  ns = fakeNightscout({ entries: [{ type: 'sgv', sgv: 110, date: NOW - 13 * MIN }] });
  cl = fakeCareLink({ data: () => pdWith() });
  globalThis.fetch = (url, init) => (new URL(url).hostname === 'ns.example' ? ns.handler(url, init) : cl.handler(url, init));
});
afterEach(() => { globalThis.fetch = realFetch; Date.now = realNow; });

const TOKENS = () => ({ access_token: jwt(NOW + 30 * MIN), refresh_token: 'rt-0', client_id: 'client', refreshed_at: NOW - 60 * MIN });
const req = (path, method = 'GET') =>
  new Request(`https://bridge.example${path}`, { method, headers: { authorization: 'Bearer clock-secret' } });

test('POST /carelink/poll uploads and reports it in the debug output', async () => {
  const env = { ...nsEnv, CLOCK_TOKEN: 'clock-secret', CARELINK: memoryKV({ tokens: TOKENS() }) };
  const dbg = await (await worker.fetch(req('/carelink/poll', 'POST'), env)).json();
  assert.equal(dbg.carelink.nightscout_upload.ok, true);
  assert.equal(dbg.carelink.nightscout_upload.entries, 2);
  assert.equal(ns.db.devicestatus.length, 1);
});

test('/clock answers first and uploads in the background via waitUntil', async () => {
  const env = { ...nsEnv, CLOCK_TOKEN: 'clock-secret', CARELINK: memoryKV({ tokens: TOKENS() }) };
  const waits = [];
  const res = await worker.fetch(req('/clock'), env, { waitUntil: (p) => waits.push(p) });
  assert.equal(res.status, 200);
  assert.equal((await res.json()).source, 'carelink');
  assert.equal(waits.length, 1);
  const up = await waits[0];
  assert.equal(up.ok, true);
  assert.equal(ns.db.devicestatus.length, 1);
});

test('a Nightscout upload failure never affects the clock, and shows in /debug', async () => {
  ns = fakeNightscout({ failPost: true });
  const env = { ...nsEnv, CLOCK_TOKEN: 'clock-secret', CARELINK: memoryKV({ tokens: TOKENS() }) };
  const res = await worker.fetch(req('/carelink/poll', 'POST'), env);
  assert.equal(res.status, 200);
  const dbg = await res.json();
  assert.equal(dbg.clock.glucose, 120);
  assert.equal(dbg.carelink.nightscout_upload.ok, false);
  assert.match(dbg.carelink.nightscout_upload.error, /HTTP 500/);
});

test('no secret configured: no upload, no Nightscout writes', async () => {
  const env = { NS_URL: 'https://ns.example', CLOCK_TOKEN: 'clock-secret', CARELINK: memoryKV({ tokens: TOKENS() }) };
  const waits = [];
  await worker.fetch(req('/clock'), env, { waitUntil: (p) => waits.push(p) });
  assert.equal(waits.length, 0);
  assert.equal(ns.posts().length, 0);
});

test('RAW_CAPTURE stores the raw patientData (no sgs, recent markers) as its own device', async () => {
  const ns = fakeNightscout();
  const pd = pdWith({ markers: [...MARKERS, { type: 'INSULIN', dateTime: isoAt(48 * 60), deliveredFastAmount: 1 }] });
  const snapshot = buildSnapshot(pd, NOW);
  let r = await uploadToNightscout(nsEnv, { snapshot, patientData: pd }, NOW, ns.handler);
  assert.equal(r.raw, undefined);
  assert.equal(ns.db.devicestatus.filter((d) => d.device === RAW_DEVICE).length, 0);
  r = await uploadToNightscout({ ...nsEnv, RAW_CAPTURE: 'true' }, { snapshot, patientData: pd }, NOW + MIN, ns.handler);
  assert.equal(r.raw, true);
  const raw = ns.db.devicestatus.find((d) => d.device === RAW_DEVICE);
  assert.equal(raw.patientData.sgs, undefined);
  assert.equal(raw.patientData.sgs_count, 289);   // fixture: 288 + one empty reading
  assert.equal(raw.patientData.markers.length, MARKERS.length);        // the 2-day-old one is dropped
  assert.equal(raw.patientData.reservoirRemainingUnits, 42.5);
  // the raw docs never count as the bridge's own devicestatus
  const own = ns.db.devicestatus.filter((d) => d.device === DEVICE);
  assert.equal(own.length, 1);
});
