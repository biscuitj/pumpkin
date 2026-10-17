import { test } from 'node:test';
import assert from 'node:assert/strict';
import { pollOnce } from '../src/carelink.js';
import { updateLatches, activeLatches, buildSnapshot, suspendState, LATCH_MAX_AGE_MS } from '../src/carelink-data.js';
import worker, { clockWarnings, _resetCache } from '../src/index.js';
import { NOW, MIN, patientData, memoryKV, jwt, fakeCareLink } from './fixtures.js';

const HOUR = 60 * MIN;
const iso = (ms) => new Date(ms).toISOString();
const TOKENS = () => ({ access_token: jwt(NOW + 100 * HOUR), refresh_token: 'rt-0', client_id: 'client', refreshed_at: NOW });

/** patientData with the given pump state; notification and marker times are absolute. */
function pdAt(t, { mode = 'AUTO_BASAL', reservoir = 113.5, battery = 75, sensorHours = 146, active = [], cleared = [], markers = [], suspended = false, banners, extraMarkers = [], iob = 1.35, inRange = true } = {}) {
  const shiftIso = iso;
  return patientData({
    extra: {
      reservoirRemainingUnits: reservoir,
      conduitMedicalDeviceInRange: inRange,
      activeInsulin: iob === null ? null : { amount: iob, datetime: iso(t - 20000), precision: 'MAX', kind: 'Insulin' },
      pumpSuspended: suspended,
      therapyAlgorithmState: { autoModeShieldState: mode },
      pumpBannerState: banners || (suspended ? [{ type: 'DELIVERY_SUSPEND' }] : []),
      lastMedicalDeviceDataUpdateServerTime: t - 20000,
      pumpBatteryLevelPercent: battery,
      sensorDurationHours: sensorHours,
      notificationHistory: {
        activeNotifications: active.map(([faultId, at]) => ({ faultId, dateTime: shiftIso(at) })),
        clearedNotifications: cleared.map(([faultId, at, clearedAt]) => ({ faultId, triggeredDateTime: shiftIso(at), dateTime: shiftIso(clearedAt) })),
      },
      markers: [
        ...markers.map(([type, at, value]) => (type === 'AUTO_BASAL_DELIVERY'
          ? { type, dateTime: shiftIso(at), bolusAmount: 0.05 } : { type, dateTime: shiftIso(at), value })),
        ...extraMarkers,
      ],
    },
  });
}

/** Run polls through the real client with a changing CareLink response. */
async function polls(steps) {
  const kv = memoryKV({ tokens: TOKENS() });
  const results = [];
  for (const [t, opts] of steps) {
    const cl = fakeCareLink({ data: () => pdAt(t, opts) });
    const r = await pollOnce({ CARELINK: kv }, t, cl.handler);
    results.push({ t, snapshot: r.snapshot, warnings: clockWarnings(r.snapshot.alerts, activeLatches(r.snapshot.latches), suspendState(r.snapshot)) });
  }
  return { kv, results };
}

test('today: set change reminder dismissed on the pump stays on; a new reservoir alone does not end it', async () => {
  const T = NOW - 3 * HOUR; // reminder at "19:56"
  const { results } = await polls([
    [T + 0, { active: [[109, T]] }],                                   // active
    [T + 15 * MIN, { cleared: [[109, T, T + 13 * MIN]] }],             // dismissed, set not changed
    [T + 60 * MIN, { reservoir: 160 }],                                // new reservoir: not proof of a new set
  ]);
  assert.deepEqual(results.map((r) => r.warnings), [['set'], ['set'], ['set']]);
});

test('set change detected from the tubing fill: reservoir drop no insulin explains, while suspended', async () => {
  const T = NOW - 3 * HOUR;
  const basal = (at) => ['AUTO_BASAL_DELIVERY', at];
  const { results } = await polls([
    [T, { reservoir: 107.4, active: [[109, T]] }],
    [T + 4 * MIN, { reservoir: 98.2, suspended: true, markers: [basal(T + 2 * MIN)] }],   // 11 Oct: -9.2 U, suspended
    [T + 8 * MIN, { reservoir: 98.2, cleared: [[109, T, T + 6 * MIN]] }],                // resumed; no re-latch
  ]);
  assert.deepEqual(results.map((r) => r.warnings), [['set'], ['suspended'], []]);
  assert.equal(results[1].snapshot.set_change_at, T + 4 * MIN - 20000);
  assert.equal(results[1].snapshot.latches.set.how, 'pump data');
});

test('no false set change: a big bolus, or a short suspend without a fill', async () => {
  const T = NOW - 3 * HOUR;
  const bolus = (at, u) => ({ type: 'INSULIN', dateTime: new Date(at).toISOString(), activationType: 'RECOMMENDED', deliveredFastAmount: u });
  const { results } = await polls([
    [T, { reservoir: 100, active: [[109, T]] }],
    [T + 5 * MIN, { reservoir: 92, extraMarkers: [bolus(T + 3 * MIN, 8)] }],          // 8 U bolus, not suspended
    [T + 20 * MIN, { reservoir: 91.6, suspended: true }],                             // bath: suspended, no fill
    [T + 35 * MIN, { reservoir: 91.5 }],
  ]);
  assert.deepEqual(results.map((r) => r.warnings), [['set'], ['set'], ['suspended', 'set'], ['set']]);
});

test('set change detected when the suspend falls between two polls: drop, no IOB rise', async () => {
  const T = NOW - 3 * HOUR;
  const { results } = await polls([
    [T, { reservoir: 45.3, iob: 1.4, active: [[109, T]] }],
    [T + 5 * MIN, { reservoir: 35.7, iob: 1.25 }],                                    // 14 Oct numbers, suspend not seen
  ]);
  assert.deepEqual(results.map((r) => r.warnings), [['set'], []]);
  assert.equal(results[1].snapshot.set_change_at, T + 5 * MIN - 20000);
});

test('no false set change without a suspend: unrecorded bolus, long gap, or no IOB', async () => {
  const T = NOW - 3 * HOUR;
  const run = (steps) => polls(steps).then(({ results }) => results.map((r) => r.warnings));
  // 6 U bolus whose record has not reached CareLink yet: IOB jumps.
  assert.deepEqual(await run([
    [T, { reservoir: 100, iob: 0.5, active: [[109, T]] }],
    [T + 5 * MIN, { reservoir: 94, iob: 6.3 }],
  ]), [['set'], ['set']]);
  // Same drop over a 60 min gap: the bolus has worn off in IOB, so not trusted.
  assert.deepEqual(await run([
    [T, { reservoir: 100, iob: 0.5, active: [[109, T]] }],
    [T + 60 * MIN, { reservoir: 94, iob: 0.4 }],
  ]), [['set'], ['set']]);
  // No IOB in the data: not trusted.
  assert.deepEqual(await run([
    [T, { reservoir: 100, iob: null, active: [[109, T]] }],
    [T + 5 * MIN, { reservoir: 90, iob: null }],
  ]), [['set'], ['set']]);
  // Drop below 5 U: not a fill.
  assert.deepEqual(await run([
    [T, { reservoir: 100, iob: 1, active: [[109, T]] }],
    [T + 5 * MIN, { reservoir: 95.2, iob: 0.9 }],
  ]), [['set'], ['set']]);
});

test('reservoir reminder still ends on a new reservoir', async () => {
  const T = NOW - 3 * HOUR;
  const { results } = await polls([
    [T, { reservoir: 15, active: [[105, T]] }],
    [T + 20 * MIN, { reservoir: 14.6 }],
    [T + 60 * MIN, { reservoir: 160 }],
  ]);
  assert.deepEqual(results.map((r) => r.warnings), [['reservoir'], ['reservoir'], []]);
});

test('reservoir reminder survives pump readings while out of range or zeroed', async () => {
  const T = NOW - 3 * HOUR;
  const { results } = await polls([
    [T, { reservoir: 15, active: [[105, T]] }],
    [T + 20 * MIN, { reservoir: 14.6, cleared: [[105, T, T + 10 * MIN]] }],  // dismissed on the pump
    [T + 30 * MIN, { reservoir: 0, battery: 0, inRange: false }],          // night: pump out of range
    [T + 40 * MIN, { reservoir: 0, battery: 0 }],                          // zeroed record, flag still true
    [T + 50 * MIN, { reservoir: 14.1 }],                                   // back: same reservoir
  ]);
  assert.deepEqual(results.map((r) => r.warnings), [['reservoir'], ['reservoir'], ['reservoir'], ['reservoir'], ['reservoir']]);
  assert.equal(results[2].snapshot.pump.reservoir_units, null);
  assert.equal(results[4].snapshot.latches.reservoir.low, 14.1);
});

test('reservoir reminder survives any bad low reading, even with the pump in range', async () => {
  const T = NOW - 3 * HOUR;
  const { results } = await polls([
    [T, { reservoir: 16.2, active: [[105, T]] }],
    [T + 20 * MIN, { reservoir: 15.9, cleared: [[105, T, T + 10 * MIN]] }],
    [T + 30 * MIN, { reservoir: 1.5 }],                                    // glitch, nothing else odd
    [T + 40 * MIN, { reservoir: 15.7 }],                                   // normal again: no refill
    [T + 90 * MIN, { reservoir: 82 }],                                     // real refill
  ]);
  assert.deepEqual(results.map((r) => r.warnings), [['reservoir'], ['reservoir'], ['reservoir'], ['reservoir'], []]);
  assert.equal(results[4].snapshot.latches.reservoir.value, 82);
});

test('battery swap: the "battery removed" alarm it raises does not restart the reminder', async () => {
  const T = NOW - 3 * HOUR;
  const { results } = await polls([
    [T, { battery: 25, active: [[104, T]] }],
    [T + 10 * MIN, { battery: 25, cleared: [[104, T, T + MIN]] }],
    [T + 60 * MIN, { battery: 100, cleared: [[104, T, T + MIN], [84, T + 55 * MIN, T + 57 * MIN]] }],  // swapped
    [T + 65 * MIN, { battery: 100, cleared: [[104, T, T + MIN], [84, T + 55 * MIN, T + 57 * MIN]] }],
  ]);
  assert.deepEqual(results.map((r) => r.warnings), [['battery'], ['battery'], [], []]);
  assert.equal(results[3].snapshot.latches.battery.done, true);
});

test('a new battery alert after the swap still starts a reminder', async () => {
  const T = NOW - 3 * HOUR;
  const { results } = await polls([
    [T, { battery: 25, active: [[104, T]] }],
    [T + 60 * MIN, { battery: 100 }],
    [T + 120 * MIN, { battery: 25, active: [[104, T + 118 * MIN]] }],
  ]);
  assert.deepEqual(results.map((r) => r.warnings), [['battery'], [], ['battery']]);
});

test('dismissed between two polls: picked up from the cleared history', async () => {
  const T = NOW - 30 * MIN;
  const { results } = await polls([[T + 20 * MIN, { cleared: [[109, T, T + 5 * MIN]] }]]);
  assert.deepEqual(results[0].warnings, ['set']);
});

test('old dismissed notifications (> 2 h) do not start a reminder', async () => {
  const T = NOW - 5 * HOUR;
  const { results } = await polls([[NOW, { cleared: [[109, T, T + 5 * MIN]] }]]);
  assert.deepEqual(results[0].warnings, []);
});

test('sensor, battery and BG reminders clear on their own evidence', () => {
  const snap = (opts) => buildSnapshot(pdAt(NOW, opts), NOW);
  // sensor change: hours left jumps up with a new sensor
  let l = updateLatches({}, snap({ sensorHours: 0, active: [[777, NOW - MIN]] }), NOW);
  assert.deepEqual(activeLatches(l), ['sensor']);
  l = updateLatches(l, snap({ sensorHours: 0 }), NOW + 10 * MIN);
  assert.deepEqual(activeLatches(l), ['sensor']);
  l = updateLatches(l, snap({ sensorHours: 167 }), NOW + 3 * HOUR);
  assert.deepEqual(activeLatches(l), []);

  // battery: goes up after a new AA
  l = updateLatches({}, snap({ battery: 25, active: [[104, NOW - MIN]] }), NOW);
  assert.deepEqual(activeLatches(l), ['battery']);
  l = updateLatches(l, snap({ battery: 25 }), NOW + HOUR);
  assert.deepEqual(activeLatches(l), ['battery']);
  l = updateLatches(l, snap({ battery: 100 }), NOW + 2 * HOUR);
  assert.deepEqual(activeLatches(l), []);

  // BG required: a finger-prick BG after the alert
  const safe = { mode: 'SAFE_BASAL' };
  l = updateLatches({}, snap({ ...safe, active: [[829, NOW - 10 * MIN]], markers: [['BG_READING', NOW - 2 * HOUR, 140]] }), NOW);
  assert.deepEqual(activeLatches(l), ['bg']);
  l = updateLatches(l, snap({ ...safe, markers: [['BG_READING', NOW - 2 * MIN, 132]] }), NOW + MIN);
  assert.deepEqual(activeLatches(l), []);
});

test('SmartGuard BG required clears when the pump is back in Auto Basal, without a finger BG', () => {
  const snap = (opts) => buildSnapshot(pdAt(NOW, opts), NOW);
  // 821 minimum delivery timeout: alert dismissed, pump stays in Safe Basal (blue bar)
  let l = updateLatches({}, snap({ mode: 'SAFE_BASAL', active: [[821, NOW - 5 * MIN]] }), NOW);
  assert.deepEqual(activeLatches(l), ['bg']);
  l = updateLatches(l, snap({ mode: 'SAFE_BASAL' }), NOW + 10 * MIN);
  assert.deepEqual(activeLatches(l), ['bg'], 'still Safe Basal: still needed');
  l = updateLatches(l, snap({ mode: 'AUTO_BASAL' }), NOW + HOUR);
  assert.deepEqual(activeLatches(l), [], 'SmartGuard is back');
  assert.equal(l.bg.how, 'pump data');

  // SmartGuard exit to manual (819): stays until a BG or Auto Basal
  l = updateLatches({}, snap({ mode: 'FEATURE_OFF', active: [[819, NOW - 5 * MIN]] }), NOW);
  l = updateLatches(l, snap({ mode: 'FEATURE_OFF' }), NOW + HOUR);
  assert.deepEqual(activeLatches(l), ['bg']);

  // the BG check reminder (103) is not about SmartGuard: only a finger BG clears it
  l = updateLatches({}, snap({ active: [[103, NOW - 5 * MIN]] }), NOW);
  l = updateLatches(l, snap({}), NOW + HOUR);
  assert.deepEqual(activeLatches(l), ['bg']);
});

test('reminders expire after 72 h; the warning triangle is never latched', () => {
  const s = buildSnapshot(pdAt(NOW, { active: [[109, NOW - MIN], [2, NOW - MIN]] }), NOW);
  let l = updateLatches({}, s, NOW);
  assert.deepEqual(activeLatches(l), ['set']);
  const quiet = buildSnapshot(pdAt(NOW, {}), NOW);
  l = updateLatches(l, quiet, NOW + LATCH_MAX_AGE_MS - MIN);
  assert.deepEqual(activeLatches(l), ['set']);
  l = updateLatches(l, quiet, NOW + LATCH_MAX_AGE_MS + MIN);
  assert.deepEqual(activeLatches(l), []);
  assert.deepEqual(clockWarnings(quiet.alerts, activeLatches(l)), []);
});

test('POST /reminders/clear marks a reminder done by hand', async () => {
  _resetCache();
  const snapshot = buildSnapshot(pdAt(NOW, {}), NOW);
  snapshot.latches = { set: { since: NOW - HOUR, code: '109', low: 113.5 } };
  const kv = memoryKV({ tokens: TOKENS(), snapshot });
  const env = { CLOCK_TOKEN: 'clock-secret', CARELINK: kv };
  const req = (q) => new Request(`https://bridge.example/reminders/clear${q}`, { method: 'POST', headers: { authorization: 'Bearer clock-secret' } });
  let res = await worker.fetch(req('?id=sensor'), env);
  assert.equal(res.status, 404);
  res = await worker.fetch(req('?id=set'), env);
  assert.equal(res.status, 200);
  assert.deepEqual((await res.json()).cleared, ['set']);
  const stored = JSON.parse(kv.store.get('snapshot'));
  assert.equal(stored.latches.set.done, true);
  assert.equal(stored.latches.set.how, 'manual');
  assert.equal((await worker.fetch(new Request('https://bridge.example/reminders/clear?id=set', { method: 'POST' }), env)).status, 401);
});

test('pump stopped by hand shows the pause; SmartGuard auto-suspend shows the general warning', async () => {
  const T = NOW - HOUR;
  const { results } = await polls([
    [T, { suspended: true }],                                                        // swimming
    [T + 5 * MIN, { suspended: true, active: [[809, T + 4 * MIN]] }],                // suspend on low
    [T + 10 * MIN, { suspended: true, banners: [{ type: 'SUSPENDED_BEFORE_LOW' }] }], // banner only
    [T + 15 * MIN, { suspended: true, active: [[2, T + 14 * MIN]] }],                // pump error while stopped
    [T + 20 * MIN, {}],                                                              // running again
  ]);
  assert.deepEqual(results.map((r) => r.warnings),
    [['suspended'], ['warning'], ['warning'], ['suspended', 'warning'], []]);
});

test('suspendState reads pumpSuspended without a banner too', () => {
  const s = buildSnapshot(pdAt(NOW, { suspended: true, banners: [] }), NOW);
  assert.equal(suspendState(s), 'manual');
  assert.equal(suspendState(buildSnapshot(pdAt(NOW, {}), NOW)), null);
});
