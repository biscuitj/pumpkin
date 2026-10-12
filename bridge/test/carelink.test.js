import { test } from 'node:test';
import assert from 'node:assert/strict';
import {
  parseCareLinkDate, deviceTimeOffsetMs, mapCareLinkTrend, classifyNotification,
  buildSnapshot, snapshotFingerprint, tempTargetFrom,
} from '../src/carelink-data.js';
import {
  pollOnce, shouldFetch, nextStatus, decodeJwtPayload, tokenExpiresAt, CareLinkAuthError,
} from '../src/carelink.js';
import { NOW, MIN, patientData, memoryKV, jwt, fakeCareLink } from './fixtures.js';

const TOKENS = () => ({
  access_token: jwt(NOW + 30 * MIN), refresh_token: 'rt-0', client_id: 'client', client_secret: 'secret',
  'mag-identifier': 'mag-1', scope: 'openid', refreshed_at: NOW - 60 * MIN,
});

// ---------------------------------------------------------------- data

test('dates: zoned, zoneless (as UTC) and epoch', () => {
  assert.equal(parseCareLinkDate('2026-10-09T13:57:00.000Z'), Date.parse('2026-10-09T13:57:00Z'));
  assert.equal(parseCareLinkDate('2026-10-09T15:57:00+02:00'), Date.parse('2026-10-09T13:57:00Z'));
  assert.equal(parseCareLinkDate('2026-10-09T13:57:00'), Date.parse('2026-10-09T13:57:00Z'));
  assert.equal(parseCareLinkDate(1791555590783), 1791555590783);
  assert.equal(parseCareLinkDate(''), null);
  assert.equal(parseCareLinkDate('garbage'), null);
});

test('zoneless pump-local times are shifted back to UTC', () => {
  const pd = patientData({ localTimes: true });
  assert.equal(deviceTimeOffsetMs(pd), -2 * 3600000);
  const snap = buildSnapshot(pd, NOW);
  assert.equal(snap.last.t, NOW - 3 * MIN);
  assert.equal(deviceTimeOffsetMs(patientData()), 0);
  assert.equal(buildSnapshot(patientData(), NOW).last.t, NOW - 3 * MIN);
});

test('snapshot: readings, 24h window, pump fields, IOB', () => {
  const snap = buildSnapshot(patientData(), NOW);
  assert.equal(snap.sgs.length, 288);
  assert.ok(snap.sgs.every((s) => s.sg > 0));
  assert.equal(snap.last.sg, 120);
  assert.deepEqual(snap.iob, { units: 1.35, at: NOW - 20000 });
  assert.equal(snap.pump.reservoir_units, 42.5);
  assert.equal(snap.pump.battery_pct, 75);
  assert.equal(snap.sensor.hours_left, 31);
  assert.equal(snap.phone.battery_pct, 88);
  assert.equal(snap.pump.auto_mode, 'AUTO_BASAL');
  assert.equal(snap.carelink_stats.time_in_range, 77);
});

test('trend mapping keeps the pump arrow count', () => {
  assert.deepEqual(mapCareLinkTrend('UP'), { trend: 'SingleUp', arrows: 1 });
  assert.deepEqual(mapCareLinkTrend('DOWN_TRIPLE'), { trend: 'DoubleDown', arrows: -3 });
  assert.equal(mapCareLinkTrend('NONE'), null);
  assert.equal(mapCareLinkTrend(undefined), null);
});

test('alerts: classified, prioritised, glucose alerts never headline', () => {
  const n = (faultId, extra = {}) => ({ faultId, dateTime: '2026-10-09T13:40:00.000Z', messageId: 'BC_X', ...extra });
  assert.equal(classifyNotification(n('105'), 'NGP').short, 'RESERVOIR BIJNA LEEG');
  assert.equal(classifyNotification(n(105), 'NGP').code, '105');
  assert.equal(classifyNotification(n('109'), 'NGP').short, 'SET VERVANGEN');
  assert.equal(classifyNotification(n('794'), 'NGP').short, 'SENSOR VERVANGEN');
  assert.equal(classifyNotification(n('798'), 'NGP').code, '797'); // alias table
  assert.equal(classifyNotification(n('802'), 'NGP').headline, false);
  const unknown = classifyNotification(n('999'), 'NGP');
  assert.equal(unknown.known, false);
  assert.equal(unknown.category, 'other');
  const gm = classifyNotification({ messageId: 'alert.sensor.replace.lifetime' }, 'GUARDIAN');
  assert.equal(gm.short, 'SENSOR VERVANGEN');

  const snap = buildSnapshot(patientData({ extra: { notificationHistory: { activeNotifications: [
    n('802'), n('105'), n('140', { alertSilenced: true }), n('999'),
  ] } } }), NOW);
  assert.deepEqual(snap.alerts.map((a) => a.code), ['140', '105', '802', '999']);
  assert.equal(snap.alerts[0].short, 'SET AANSLUITEN');
  assert.equal(snap.alerts[0].silenced, true);
  assert.equal(snap.alerts.find((a) => a.code === '802').headline, false);
});

test('temp target from the pump banner', () => {
  const snap = buildSnapshot(patientData({ extra: { pumpBannerState: [{ type: 'TEMP_TARGET', timeRemaining: 95 }] } }), NOW);
  assert.deepEqual(tempTargetFrom(snap), { active: true, minutes_left: 95 });
  assert.deepEqual(tempTargetFrom(buildSnapshot(patientData(), NOW)), { active: false, minutes_left: null });
});

test('fingerprint ignores fetch time, notices data changes', () => {
  const a = buildSnapshot(patientData(), NOW);
  const b = buildSnapshot(patientData(), NOW + MIN);
  assert.equal(snapshotFingerprint(a), snapshotFingerprint(b));
  const c = buildSnapshot(patientData({ extra: { activeInsulin: { amount: 2 } } }), NOW);
  assert.notEqual(snapshotFingerprint(a), snapshotFingerprint(c));
});

// ---------------------------------------------------------------- client

test('jwt helpers', () => {
  const t = jwt(NOW);
  assert.equal(decodeJwtPayload(t).token_details.country, 'BE');
  assert.equal(tokenExpiresAt({ access_token: t }), Math.floor(NOW / 1000) * 1000);
  assert.equal(tokenExpiresAt({ access_token: t, expires_at: 5 }), 5);
  assert.equal(decodeJwtPayload('opaque'), null);
});

test('first poll: discovery, identity, data; writes tokens? no, config and snapshot', async () => {
  const cl = fakeCareLink();
  const kv = memoryKV({ tokens: TOKENS() });
  const r = await pollOnce({ CARELINK: kv }, NOW, cl.handler);
  assert.equal(r.snapshot.last.sg, 120);
  assert.equal(r.refreshed, false);
  assert.deepEqual(kv.puts.sort(), ['config', 'snapshot']);
  const config = JSON.parse(kv.store.get('config'));
  assert.equal(config.token_url, 'https://auth.example:443/oauth/token');
  assert.deepEqual(config.identity, { role: 'CARE_PARTNER_OUS', care_partner: true, username: 'cp-user', patient: 'patient-a' });
  const msg = cl.calls.find((c) => c.url.pathname.endsWith('/display/message'));
  assert.deepEqual(JSON.parse(msg.body), { username: 'cp-user', role: 'carepartner', appVersion: '3.8.0', patientId: 'patient-a' });
  assert.equal(msg.headers['mag-identifier'], 'mag-1');
  assert.match(msg.headers.Authorization, /^Bearer /);
});

test('unchanged data is not rewritten; changed data is', async () => {
  const cl = fakeCareLink();
  const kv = memoryKV({ tokens: TOKENS() });
  await pollOnce({ CARELINK: kv }, NOW, cl.handler);
  kv.puts.length = 0;
  const r = await pollOnce({ CARELINK: kv }, NOW + MIN, cl.handler);
  assert.equal(r.wrote, false);
  assert.deepEqual(kv.puts, []);
  const cl2 = fakeCareLink({ data: () => patientData({ lastAgeMin: -2 }) });
  const r2 = await pollOnce({ CARELINK: kv }, NOW + 2 * MIN, cl2.handler);
  assert.equal(r2.wrote, true);
});

test('expiring token is refreshed first and the rotated refresh token stored', async () => {
  const cl = fakeCareLink();
  const tokens = { ...TOKENS(), access_token: jwt(NOW + 2 * MIN) };
  const kv = memoryKV({ tokens });
  const r = await pollOnce({ CARELINK: kv }, NOW, cl.handler);
  assert.equal(r.refreshed, true);
  const stored = JSON.parse(kv.store.get('tokens'));
  assert.equal(stored.refresh_token, 'rt-1');
  assert.equal(stored.expires_at, NOW + 3600 * 1000);
  const refresh = cl.calls.find((c) => c.url.pathname === '/oauth/token');
  const form = new URLSearchParams(refresh.body);
  assert.equal(form.get('grant_type'), 'refresh_token');
  assert.equal(form.get('refresh_token'), 'rt-0');
  assert.equal(form.get('client_secret'), 'secret');
  assert.equal(refresh.headers['mag-identifier'], 'mag-1');
});

test('401 on data: refresh once and retry', async () => {
  const cl = fakeCareLink({ displayStatuses: [401] });
  const kv = memoryKV({ tokens: TOKENS() });
  const r = await pollOnce({ CARELINK: kv }, NOW, cl.handler);
  assert.equal(r.refreshed, true);
  assert.equal(r.snapshot.last.sg, 120);
});

test('rejected refresh is an auth error (needs a new browser login)', async () => {
  const cl = fakeCareLink({ refreshStatus: 400 });
  const kv = memoryKV({ tokens: { ...TOKENS(), access_token: jwt(NOW - MIN) } });
  await assert.rejects(pollOnce({ CARELINK: kv }, NOW, cl.handler), CareLinkAuthError);
});

test('Auth0-style 403 invalid_grant is an auth error that says why', async () => {
  const cl = fakeCareLink({ refreshStatus: 403 });
  const kv = memoryKV({ tokens: { ...TOKENS(), access_token: jwt(NOW - MIN) } });
  await assert.rejects(pollOnce({ CARELINK: kv }, NOW, cl.handler), (e) =>
    e instanceof CareLinkAuthError && /HTTP 403 invalid_grant: Unknown or invalid refresh token/.test(e.message)
    && /auth\.example/.test(e.message));
});

test('403 without an OAuth error (firewall page) is not a login error', async () => {
  const cl = fakeCareLink({ refreshStatus: 403, refreshBody: '<html><body><h1>Access denied</h1> Ref 18.abc</body></html>' });
  const kv = memoryKV({ tokens: { ...TOKENS(), access_token: jwt(NOW - MIN) } });
  await assert.rejects(pollOnce({ CARELINK: kv }, NOW, cl.handler), (e) =>
    !(e instanceof CareLinkAuthError) && /HTTP 403 text\/html: Access denied Ref 18\.abc/.test(e.message));
  // the stored refresh token is untouched, so a later attempt can still use it
  assert.equal(JSON.parse(kv.store.get('tokens')).refresh_token, 'rt-0');
});

test('fresh logindata.json (no refreshed_at) uses its access token before refreshing', async () => {
  const cl = fakeCareLink();
  const { refreshed_at, ...fresh } = TOKENS();
  const kv = memoryKV({ tokens: { ...fresh, access_token: jwt(NOW + 50 * MIN, undefined, NOW - 10 * MIN) } });
  const r = await pollOnce({ CARELINK: kv }, NOW, cl.handler);
  assert.equal(r.refreshed, false);
  assert.equal(r.snapshot.last.sg, 120);
  assert.equal(cl.refreshCount, 0);
});

test('refresh request sends the app headers', async () => {
  const cl = fakeCareLink();
  const kv = memoryKV({ tokens: { ...TOKENS(), access_token: jwt(NOW + 2 * MIN) } });
  await pollOnce({ CARELINK: kv }, NOW, cl.handler);
  const refresh = cl.calls.find((c) => c.url.pathname === '/oauth/token');
  assert.match(refresh.headers['User-Agent'], /Dalvik/);
  assert.equal(refresh.headers.Accept, 'application/json');
});

test('missing tokens is an auth error', async () => {
  await assert.rejects(pollOnce({ CARELINK: memoryKV() }, NOW, fakeCareLink().handler), CareLinkAuthError);
});

test('env overrides skip identity lookups', async () => {
  const cl = fakeCareLink({ patients: [] });
  const kv = memoryKV({ tokens: TOKENS() });
  await pollOnce({ CARELINK: kv, CARELINK_PATIENT_USERNAME: 'patient-x', CARELINK_USERNAME: 'me' }, NOW, cl.handler);
  const msg = cl.calls.find((c) => c.url.pathname.endsWith('/display/message'));
  assert.deepEqual(JSON.parse(msg.body).patientId, 'patient-x');
  assert.equal(JSON.parse(msg.body).username, 'me');
});

// ---------------------------------------------------------------- scheduling

test('shouldFetch follows the sensor rhythm', () => {
  const snap = { fetched_at: NOW - 2 * MIN, last: { t: NOW - 3 * MIN } };
  // next reading at last + 5 min + 75 s = NOW + 3:15
  assert.equal(shouldFetch(snap, null, NOW, 0, 75).fetch, false);
  assert.equal(shouldFetch(snap, null, NOW + 3 * MIN + 16000, 0, 75).fetch, true);
  // late: at most every 20 s in the first 5 minutes
  assert.equal(shouldFetch(snap, null, NOW + 4 * MIN, NOW + 3 * MIN + 50000, 75).fetch, false);
  assert.equal(shouldFetch(snap, null, NOW + 4 * MIN, NOW + 3 * MIN + 30000, 75).fetch, true);
  // backoff after errors wins
  assert.equal(shouldFetch(snap, { next_retry_at: NOW + 10 * MIN }, NOW + 5 * MIN, 0, 75).fetch, false);
  assert.equal(shouldFetch(null, null, NOW, 0, 75).fetch, true);
});

test('error backoff doubles, auth errors wait an hour, success resets', () => {
  const s1 = nextStatus(null, new Error('boom'), NOW);
  assert.equal(s1.failures, 1);
  assert.equal(s1.next_retry_at, NOW + MIN);
  const s3 = nextStatus(nextStatus(s1, new Error('boom'), NOW), new Error('boom'), NOW);
  assert.equal(s3.next_retry_at, NOW + 4 * MIN);
  const auth = nextStatus(null, new CareLinkAuthError('login'), NOW);
  assert.equal(auth.auth_error, true);
  assert.equal(auth.next_retry_at, NOW + 60 * MIN);
  assert.deepEqual(nextStatus(s3, null, NOW), { failures: 0, last_ok_at: NOW });
  assert.equal(nextStatus(null, null, NOW), null);
});
