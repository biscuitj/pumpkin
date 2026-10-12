// Pure logic for the Pumpkin bridge. No I/O here, so it is unit-testable.

const MIN = 60 * 1000;

export const FEED_DIRECTIONS = new Set([
  'TripleUp', 'DoubleUp', 'SingleUp', 'FortyFiveUp', 'Flat',
  'FortyFiveDown', 'SingleDown', 'DoubleDown', 'TripleDown',
]);

/** Keep plausible sgv entries, newest first, one per timestamp. */
export function cleanEntries(entries) {
  const seen = new Set();
  return (Array.isArray(entries) ? entries : [])
    .filter((e) => e && Number.isFinite(e.sgv) && e.sgv >= 39 && e.sgv <= 600 && Number.isFinite(e.date))
    .sort((a, b) => b.date - a.date)
    .filter((e) => (seen.has(e.date) ? false : (seen.add(e.date), true)));
}

/** Least-squares slope in mg/dL per minute over readings within `windowMin` of the newest. */
export function slopePerMinute(entries, windowMin = 20) {
  if (entries.length < 2) return null;
  const newest = entries[0].date;
  const pts = entries.filter((e) => newest - e.date <= windowMin * MIN);
  if (pts.length < 2) return null;
  const span = (newest - pts[pts.length - 1].date) / MIN;
  if (span < 4.5) return null;
  const xs = pts.map((e) => (e.date - newest) / MIN);
  const ys = pts.map((e) => e.sgv);
  const mx = xs.reduce((a, b) => a + b, 0) / xs.length;
  const my = ys.reduce((a, b) => a + b, 0) / ys.length;
  let num = 0;
  let den = 0;
  for (let i = 0; i < xs.length; i++) {
    num += (xs[i] - mx) * (ys[i] - my);
    den += (xs[i] - mx) ** 2;
  }
  return den === 0 ? null : num / den;
}

/** Dexcom-style direction from a slope (mg/dL/min). */
export function directionFromSlope(s) {
  if (s === null || !Number.isFinite(s)) return 'NONE';
  if (s >= 3) return 'DoubleUp';
  if (s >= 2) return 'SingleUp';
  if (s >= 1) return 'FortyFiveUp';
  if (s > -1) return 'Flat';
  if (s > -2) return 'FortyFiveDown';
  if (s > -3) return 'SingleDown';
  return 'DoubleDown';
}

/**
 * Trend for the clock. A real direction from the feed wins (Triple* is mapped to
 * Double*, the strongest arrow the clock draws). "NOT COMPUTABLE" and friends
 * fall back to the slope of recent readings; too little data gives "NONE".
 */
export function computeTrend(entries) {
  const latest = entries[0];
  if (latest && FEED_DIRECTIONS.has(latest.direction)) {
    const trend = latest.direction.replace('Triple', 'Double');
    return { trend, source: 'feed', slope: null };
  }
  const slope = slopePerMinute(entries);
  return { trend: directionFromSlope(slope), source: slope === null ? 'none' : 'slope', slope };
}

/** Round percentages so they add up to exactly 100 (largest remainder). */
function roundTo100(parts) {
  const floors = parts.map(Math.floor);
  let rest = 100 - floors.reduce((a, b) => a + b, 0);
  parts
    .map((p, i) => ({ i, r: p - Math.floor(p) }))
    .sort((a, b) => b.r - a.r)
    .forEach(({ i }) => { if (rest > 0) { floors[i]++; rest--; } });
  return floors;
}

/**
 * Time in range over the last 24h. Coverage is the share of 5-minute slots
 * that have a reading; below `minCoverage` the result is null (not enough data
 * to be meaningful).
 */
export function computeTir(entries, nowMs, { low = 70, high = 180, minCoverage = 0.5 } = {}) {
  const since = nowMs - 24 * 60 * MIN;
  const day = entries.filter((e) => e.date >= since && e.date <= nowMs);
  const slots = new Set(day.map((e) => Math.floor(e.date / (5 * MIN))));
  const coverage = Math.min(1, slots.size / 288);
  if (day.length === 0 || coverage < minCoverage) {
    return { in_range: -1, below: -1, above: -1, coverage, readings: day.length };
  }
  const below = day.filter((e) => e.sgv < low).length;
  const above = day.filter((e) => e.sgv > high).length;
  const inRange = day.length - below - above;
  const [b, i, a] = roundTo100([below, inRange, above].map((n) => (100 * n) / day.length));
  return { in_range: i, below: b, above: a, coverage, readings: day.length };
}

/**
 * Active insulin from Nightscout's /api/v2/properties/iob. Returns units rounded
 * to 0.01, or null when missing, non-numeric or older than maxAgeMin.
 */
export function extractIob(props, nowMs, maxAgeMin = 15) {
  const iob = props && props.iob;
  if (!iob || typeof iob.iob !== 'number' || !Number.isFinite(iob.iob)) return { units: null, reason: 'missing' };
  if (Number.isFinite(iob.mills) && nowMs - iob.mills > maxAgeMin * 60000) {
    return { units: null, reason: 'stale', age_min: Math.round((nowMs - iob.mills) / 60000) };
  }
  return { units: Math.round(iob.iob * 100) / 100, reason: 'ok', source: iob.source || null };
}

/** Insert time (ms) encoded in a MongoDB ObjectId, or null for other ids. */
export function objectIdTime(id) {
  return typeof id === 'string' && /^[0-9a-f]{24}$/i.test(id) ? parseInt(id.slice(0, 8), 16) * 1000 : null;
}

/**
 * Seconds until the clock should poll again.
 *
 * Nightscout's CareLink poller (nightscout-connect) polls aligned to the sensor:
 * last reading time + 5 min + a ~68 s buffer (+ up to 18 s jitter). If CareLink
 * doesn't have the reading yet, it waits for the next 5-minute slot. So the next
 * arrival is predictable from the newest reading's sensor time plus the observed
 * upload lag of on-time readings (insert time from the MongoDB ObjectId).
 * If a slot passes without data, check at least every `lateMaxSec` so a broken
 * assumption is never worse than fixed polling.
 */
export function nextPollSec(entries, nowMs, {
  marginSec = 5, minSec = 10, maxSec = 300, lateMaxSec = 60,
  cadenceSec = 300, defaultLagSec = 86, onTimeMaxLagSec = 150,
} = {}) {
  const latest = entries[0];
  if (!latest || !Number.isFinite(latest.date) || latest.date > nowMs + 60000) {
    return { next_poll_sec: null, reason: 'no recent reading' };
  }
  const lags = entries
    .map((e) => (objectIdTime(e._id) - e.date) / 1000)
    .filter((l) => Number.isFinite(l) && l >= 0 && l <= onTimeMaxLagSec);
  const lagSec = lags.length >= 2 ? Math.max(...lags) : defaultLagSec;
  const offset = (lagSec + marginSec) * 1000;
  const cadence = cadenceSec * 1000;
  const n = Math.max(1, Math.ceil((nowMs - latest.date - offset) / cadence + 1e-9));
  const target = latest.date + n * cadence + offset;
  let wait = (target - nowMs) / 1000;
  const late = n > 1;
  if (late) wait = Math.min(wait, lateMaxSec);
  return {
    next_poll_sec: Math.round(Math.min(maxSec, Math.max(minSec, wait))),
    upload_lag_s: lagSec,
    lag_source: lags.length >= 2 ? 'observed' : 'default',
    expected_at: new Date(target).toISOString(),
    late,
  };
}
