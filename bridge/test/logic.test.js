import { test } from 'node:test';
import assert from 'node:assert/strict';
import {
  cleanEntries, computeTrend, directionFromSlope, computeTir, extractIob,
  objectIdTime, nextPollSec,
} from '../src/logic.js';

const MIN = 60000;
const NOW = Date.parse('2026-10-09T08:10:00Z'); // 10:10 Brussels, Friday

const series = (values, dir = 'NOT COMPUTABLE', step = 5) =>
  values.map((sgv, i) => ({ sgv, date: NOW - i * step * MIN, direction: dir }));

test('cleanEntries drops junk, dedupes, sorts newest first', () => {
  const out = cleanEntries([
    { sgv: 120, date: NOW - 5 * MIN }, { sgv: 130, date: NOW }, { sgv: 130, date: NOW },
    { sgv: 20, date: NOW - MIN }, { sgv: 'x', date: NOW }, null,
  ]);
  assert.deepEqual(out.map((e) => e.sgv), [130, 120]);
});

test('feed direction wins; Triple maps to Double', () => {
  assert.equal(computeTrend(series([200, 190], 'SingleDown')).trend, 'SingleDown');
  assert.equal(computeTrend(series([200, 180], 'TripleUp')).trend, 'DoubleUp');
});

test('NOT COMPUTABLE falls back to slope', () => {
  // falling 10 mg/dL per 5 min = -2/min -> SingleDown boundary (> -3, <= -2)
  assert.equal(computeTrend(series([180, 190, 200, 210])).trend, 'SingleDown');
  assert.equal(computeTrend(series([202, 201, 203, 202])).trend, 'Flat');
  assert.equal(computeTrend(series([200])).trend, 'NONE');
});

test('slope needs data spanning at least ~5 minutes', () => {
  const close = [{ sgv: 200, date: NOW }, { sgv: 190, date: NOW - 2 * MIN }];
  assert.equal(computeTrend(close).trend, 'NONE');
});

test('direction thresholds', () => {
  assert.equal(directionFromSlope(3.2), 'DoubleUp');
  assert.equal(directionFromSlope(1.5), 'FortyFiveUp');
  assert.equal(directionFromSlope(-0.5), 'Flat');
  assert.equal(directionFromSlope(-3.5), 'DoubleDown');
  assert.equal(directionFromSlope(null), 'NONE');
});

test('TIR percentages add up to 100 and respect coverage', () => {
  const day = [];
  for (let i = 0; i < 288; i++) {
    const sgv = i < 20 ? 60 : i < 220 ? 140 : 220; // 20 below, 200 in, 68 above
    day.push({ sgv, date: NOW - i * 5 * MIN });
  }
  const tir = computeTir(day, NOW);
  assert.equal(tir.below + tir.in_range + tir.above, 100);
  assert.equal(tir.in_range, 69);
  assert.equal(tir.below, 7);
  assert.equal(tir.coverage, 1);

  const sparse = day.slice(0, 100); // ~35% coverage
  assert.equal(computeTir(sparse, NOW).in_range, -1);
});

test('extractIob handles shapes and freshness', () => {
  assert.equal(extractIob({ iob: { iob: 1.256, mills: NOW } }, NOW).units, 1.26);
  assert.equal(extractIob({ iob: { iob: 0.8 } }, NOW).units, 0.8); // no timestamp: accept
  assert.equal(extractIob({ iob: { iob: 1, mills: NOW - 20 * MIN } }, NOW).units, null);
  assert.equal(extractIob({ iob: {} }, NOW).units, null);
  assert.equal(extractIob({}, NOW).units, null);
  assert.equal(extractIob(null, NOW).units, null);
});

// Real ns10be sample (2026-10-09): ids carry the insert time, batches share one.
const REAL = [
  ['6ac8c412087277253b1f0268', 1791542220000], ['6ac8c2e5087277253b1ef536', 1791541920000],
  ['6ac8c2e5087277253b1ef535', 1791541620000], ['6ac8c08c087277253b1eda53', 1791541320000],
  ['6ac8bf60087277253b1ecc91', 1791541020000], ['6ac8bf60087277253b1ecc90', 1791540720000],
  ['6ac8be33087277253b1ebe6d', 1791540420000], ['6ac8be33087277253b1ebe6c', 1791540120000],
  ['6ac8be33087277253b1ebe6b', 1791539820000], ['6ac8be33087277253b1ebe6a', 1791539520000],
  ['6ac8be33087277253b1ebe69', 1791539220000], ['6ac8b855087277253b1e7990', 1791538920000],
].map(([_id, date]) => ({ _id, date, sgv: 100 }));
const LAST_UPLOAD = Date.parse('2026-10-09T10:38:10Z');

test('objectIdTime decodes insert time', () => {
  assert.equal(objectIdTime('6ac8c412087277253b1f0268'), LAST_UPLOAD);
  assert.equal(objectIdTime('custom-id'), null);
});

test('next poll lands just after sensor time + 5 min + observed upload lag', () => {
  // newest reading 10:37:00, on-time lags 68-70 s -> next arrival ~10:43:10, poll 10:43:15
  const r = nextPollSec(REAL, LAST_UPLOAD + 30000); // 10:38:40
  assert.equal(r.upload_lag_s, 70);
  assert.equal(r.lag_source, 'observed');
  assert.equal(r.expected_at, '2026-10-09T10:43:15.000Z');
  assert.equal(r.next_poll_sec, 275);
  assert.equal(r.late, false);
});

test('after a missed slot it checks at most every 60 s, then aims at the next slot', () => {
  const r = nextPollSec(REAL, Date.parse('2026-10-09T10:43:20Z'));
  assert.equal(r.late, true);
  assert.equal(r.next_poll_sec, 60);
  const r2 = nextPollSec(REAL, Date.parse('2026-10-09T10:47:50Z'));
  assert.equal(r2.next_poll_sec, 25); // 10:48:15
});

test('without ObjectIds it falls back to the default lag', () => {
  const r = nextPollSec(REAL.map(({ _id, ...e }) => e), LAST_UPLOAD + 30000);
  assert.equal(r.lag_source, 'default');
  assert.equal(r.expected_at, '2026-10-09T10:43:31.000Z'); // 10:42:00 + 86 + 5
});

test('no reading means no hint', () => {
  assert.equal(nextPollSec([], LAST_UPLOAD).next_poll_sec, null);
});

test('just before the expected slot is clamped to the minimum', () => {
  const r = nextPollSec(REAL, Date.parse('2026-10-09T10:43:12Z'));
  assert.equal(r.next_poll_sec, 10);
});
