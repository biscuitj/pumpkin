// Pure transforms for CareLink care-partner data (display/message -> patientData).
// No I/O here; unit-tested against fixtures.

import { CODE_ALIASES, NGP_TEXTS, GUARDIAN_TEXTS } from './alert-texts.js';

const MIN = 60 * 1000;
const HOUR = 60 * MIN;

/** Parse a CareLink date: ISO with zone, ISO without zone (treated as UTC, like xDrip), or epoch ms. */
export function parseCareLinkDate(value) {
  if (value === null || value === undefined || value === '') return null;
  if (typeof value === 'number') return Number.isFinite(value) && value > 0 ? value : null;
  const s = String(value);
  const hasZone = /([zZ]|[+-]\d{2}:?\d{2})$/.test(s);
  const ms = Date.parse(hasZone ? s : `${s}Z`);
  return Number.isFinite(ms) && ms > 0 ? ms : null;
}

/**
 * Some CareLink timestamps carry pump-local time without a zone. Like xDrip, derive
 * the whole-hour offset from the conduit time vs. the server's receive time and
 * shift device timestamps by it. Returns the offset in ms (0 when not needed).
 */
export function deviceTimeOffsetMs(pd) {
  const device = parseCareLinkDate(pd.lastConduitDateTime);
  const server = Number(pd.lastConduitUpdateServerDateTime || pd.lastConduitUpdateServerTime);
  if (!device || !Number.isFinite(server) || server <= 0) return 0;
  const hours = Math.round((server - device) / HOUR);
  return hours !== 0 && Math.abs(hours) < 26 ? hours * HOUR : 0;
}

const TREND_MAP = {
  UP: { trend: 'SingleUp', arrows: 1 },
  UP_DOUBLE: { trend: 'DoubleUp', arrows: 2 },
  UP_TRIPLE: { trend: 'DoubleUp', arrows: 3 }, // SugarClock has no triple arrow
  DOWN: { trend: 'SingleDown', arrows: -1 },
  DOWN_DOUBLE: { trend: 'DoubleDown', arrows: -2 },
  DOWN_TRIPLE: { trend: 'DoubleDown', arrows: -3 },
};

/** Medtronic trend -> SugarClock direction; null for NONE/unknown (caller falls back to slope). */
export function mapCareLinkTrend(value) {
  return TREND_MAP[String(value || '').toUpperCase()] || null;
}

// Alert categories, highest priority first. `short` is what a clock can scroll.
const CATEGORIES = [
  { id: 'delivery', short: 'TOEDIENING GESTOPT', ngp: ['002', '007', '012', '024', '025', '029', '037', '061', '077', '130'] },
  { id: 'set_connect', short: 'SET AANSLUITEN', ngp: ['140'] },
  { id: 'battery_now', short: 'BATTERIJ VERVANGEN', ngp: ['006', '011', '057', '058', '073', '084'] },
  { id: 'reservoir_empty', short: 'RESERVOIR LEEG', ngp: ['066', '069', '113'] },
  { id: 'sensor_change', short: 'SENSOR VERVANGEN', ngp: ['777', '794'], gm: ['alert.sensor.replace.calibrationError', 'alert.sensor.replace.sensorError', 'alert.sensor.replace.lifetime'] },
  { id: 'set_change', short: 'SET VERVANGEN', ngp: ['109'] },
  { id: 'reservoir_low', short: 'RESERVOIR BIJNA LEEG', ngp: ['105'] },
  { id: 'battery_low', short: 'BATTERIJ BIJNA LEEG', ngp: ['104'] },
  { id: 'transmitter', short: 'ZENDER OPLADEN', ngp: ['779', '870'], gm: ['alert.transmitter.battery'] },
  { id: 'sensor_signal', short: 'GEEN SENSORSIGNAAL', ngp: ['780', '795', '796'], gm: ['alert.transmitter.signal', 'alert.transmitter.error', 'alert.sensor.error'] },
  { id: 'bg_required', short: 'BG METEN', ngp: ['103', '819', '821', '822', '829'] },
  { id: 'calibration', short: 'KALIBREREN', ngp: ['775', '776', '801', '832', '869'], gm: ['alert.calibration.reminder', 'alert.sensor.calibration.calibrate_now', 'alert.sensor.calibration.rejected'] },
  { id: 'meal_bolus', short: 'MAALTIJDBOLUS?', ngp: ['107'] },
  { id: 'phone', short: 'TELEFOON BATTERIJ', gm: ['alert.receiver.battery.low'] },
  // Glucose alerts: the number on the clock already says it; never a headline.
  { id: 'glucose', short: '', headline: false, ngp: ['784', '802', '803', '805', '807', '809', '810', '812', '814', '815', '816', '817', '823', '827', '833'], gm: ['alert.sg.predictive.high', 'alert.sg.predictive.low', 'alert.sg.rate.falling', 'alert.sg.rate.rising', 'alert.sg.threshold.high', 'alert.sg.threshold.low', 'alert.sg.threshold.low.urgent'] },
  { id: 'info', short: '', headline: false, ngp: ['051', '052', '062', '100', '110', '117', '797'], gm: ['alert.sensor.connection'] },
];
const BY_NGP = new Map();
const BY_GM = new Map();
CATEGORIES.forEach((c, rank) => {
  (c.ngp || []).forEach((code) => BY_NGP.set(code, { ...c, rank }));
  (c.gm || []).forEach((code) => BY_GM.set(code, { ...c, rank }));
});
const OTHER = { id: 'other', short: '', rank: CATEGORIES.length };

/** Classify one active notification. */
export function classifyNotification(n, deviceFamily) {
  const guardian = String(deviceFamily || '').toUpperCase() === 'GUARDIAN';
  let code;
  let text;
  let cat;
  if (guardian) {
    code = n.messageId != null ? String(n.messageId) : null;
    text = code ? GUARDIAN_TEXTS[code] : undefined;
    cat = code ? BY_GM.get(code) : undefined;
  } else {
    const raw = n.faultId != null ? String(n.faultId) : (n.messageId != null ? String(n.messageId) : null);
    const padded = raw && /^\d+$/.test(raw) ? raw.padStart(3, '0') : raw;
    code = padded ? (CODE_ALIASES[padded] || padded) : null;
    text = code ? NGP_TEXTS[code] : undefined;
    cat = code ? BY_NGP.get(code) : undefined;
  }
  const c = cat || OTHER;
  const known = Boolean(text);
  return {
    code: code || 'unknown',
    raw_fault_id: n.faultId ?? null,
    raw_message_id: n.messageId ?? null,
    category: c.id,
    rank: c.rank,
    headline: c.headline !== false,
    text: text || `Pump alert ${code || '?'}`,
    short: c.short || (text ? text.toUpperCase() : `POMP ALARM ${code || ''}`.trim()),
    known,
    silenced: n.alertSilenced === true,
  };
}

/** Newest finger-prick BG (BG_READING / CALIBRATION marker) in ms, or null. */
export function lastFingerBgAt(pd, offset = 0) {
  let newest = null;
  for (const m of Array.isArray(pd.markers) ? pd.markers : []) {
    const type = String(m.type || '').toUpperCase();
    if (type !== 'BG_READING' && type !== 'CALIBRATION') continue;
    const t = parseCareLinkDate(m.timestamp ?? m.dateTime ?? (m.data && m.data.timestamp));
    if (t && (!newest || t + offset > newest)) newest = t + offset;
  }
  return newest;
}

/** Marker field: flat in some API versions, under data.dataValues in others. */
function markerField(m, key) {
  if (m[key] !== undefined) return m[key];
  const dv = m.data && m.data.dataValues;
  return dv && dv[key] !== undefined ? dv[key] : undefined;
}

/** Insulin the pump recorded (boluses + auto basal) with marker time in (fromMs, toMs]. */
export function insulinRecorded(pd, fromMs, toMs, offset = 0) {
  let units = 0;
  for (const m of Array.isArray(pd.markers) ? pd.markers : []) {
    const type = String(m.type || '').toUpperCase();
    if (type !== 'INSULIN' && type !== 'AUTO_BASAL_DELIVERY') continue;
    const raw = parseCareLinkDate(m.timestamp ?? m.dateTime ?? (m.data && m.data.timestamp));
    if (!raw) continue;
    const t = raw + offset;
    if (t <= fromMs || t > toMs) continue;
    const amount = type === 'INSULIN'
      ? (Number(markerField(m, 'deliveredFastAmount')) || 0) + (Number(markerField(m, 'deliveredExtendedAmount')) || 0)
      : Number(markerField(m, 'bolusAmount')) || 0;
    if (Number.isFinite(amount)) units += amount;
  }
  return units;
}

// SmartGuard stops delivery by itself on (or before) a low: alerts 809/810, or a
// banner like SUSPENDED_ON_LOW / SUSPENDED_BEFORE_LOW (names not yet seen in our
// own data, so match loosely).
const AUTO_SUSPEND_CODES = new Set(['809', '810']);
const AUTO_SUSPEND_BANNER = /SUSPEND.*(ON|BEFORE)_LOW/;

/**
 * Why delivery is suspended right now: 'auto' (SmartGuard, low glucose), 'manual'
 * (stopped on the pump, e.g. to go swimming or change the set) or null (running).
 */
export function suspendState(snap) {
  if (!snap) return null;
  const banners = snap.banners || [];
  const auto = (snap.alerts || []).some((a) => AUTO_SUSPEND_CODES.has(a.code))
    || banners.some((b) => AUTO_SUSPEND_BANNER.test(b.type));
  if (auto) return 'auto';
  const stopped = (snap.pump && snap.pump.suspended === true)
    || banners.some((b) => b.type === 'DELIVERY_SUSPEND');
  return stopped ? 'manual' : null;
}

// A new infusion set's tubing is filled from the reservoir while the pump is
// suspended: the reservoir drops by several units that no bolus or basal explains.
// (Seen on 11 Oct: -9.2 U in 4 min, suspended, 0.05 U recorded.)
export const SET_FILL_MIN_UNITS = 5;

/** Time of a tubing fill (= set change) between the previous and this snapshot, or null. */
export function detectSetChange(prev, snap, pd) {
  if (!prev || !prev.pump || prev.pump.reservoir_units === null || snap.pump.reservoir_units === null) return null;
  const from = prev.device_data_at || prev.fetched_at;
  const to = snap.device_data_at || snap.fetched_at;
  if (!from || !to || to <= from) return null;
  const suspended = prev.pump.suspended === true || snap.pump.suspended === true
    || snap.banners.some((b) => b.type === 'DELIVERY_SUSPEND')
    || (prev.banners || []).some((b) => b.type === 'DELIVERY_SUSPEND');
  if (!suspended) return null;
  const drop = prev.pump.reservoir_units - snap.pump.reservoir_units;
  const recorded = insulinRecorded(pd, from - 2 * MIN, to, snap.offset_ms || 0);
  return drop - recorded >= SET_FILL_MIN_UNITS ? to : null;
}

/** Build the compact snapshot stored in KV from patientData. */
export function buildSnapshot(pd, fetchedAtMs) {
  if (!pd || typeof pd !== 'object') throw new Error('CareLink response has no patientData');
  const offset = deviceTimeOffsetMs(pd);
  const shift = (ms) => (ms ? ms + offset : null);

  const sgs = (Array.isArray(pd.sgs) ? pd.sgs : [])
    .map((s) => ({ t: shift(parseCareLinkDate(s.timestamp ?? s.datetime)), sg: Number(s.sg) }))
    .filter((s) => s.t && Number.isFinite(s.sg) && s.sg >= 40 && s.sg <= 400 && s.t <= fetchedAtMs + 5 * MIN)
    .sort((a, b) => a.t - b.t);
  const dedup = [];
  for (const s of sgs) if (!dedup.length || dedup[dedup.length - 1].t !== s.t) dedup.push(s);
  const last = dedup.length ? dedup[dedup.length - 1] : null;

  const family = pd.medicalDeviceFamily || null;
  const active = pd.notificationHistory && Array.isArray(pd.notificationHistory.activeNotifications)
    ? pd.notificationHistory.activeNotifications : [];
  const alerts = active
    .map((n) => ({ ...classifyNotification(n, family), since: shift(parseCareLinkDate(n.dateTime)) }))
    .sort((a, b) => a.rank - b.rank || (b.since || 0) - (a.since || 0));

  // Recently dismissed alerts: a reminder dismissed between two polls is only here.
  const clearedRaw = pd.notificationHistory && Array.isArray(pd.notificationHistory.clearedNotifications)
    ? pd.notificationHistory.clearedNotifications : [];
  const cleared = clearedRaw
    .map((n) => {
      const c = classifyNotification(n, family);
      const clearedAt = shift(parseCareLinkDate(n.dateTime));
      const triggered = shift(parseCareLinkDate(n.triggeredDateTime)) || clearedAt;
      return { code: c.code, category: c.category, triggered, cleared_at: clearedAt };
    })
    .filter((c) => c.triggered && c.triggered >= fetchedAtMs - 24 * HOUR)
    .sort((a, b) => b.triggered - a.triggered)
    .slice(0, 10);

  const banners = (Array.isArray(pd.pumpBannerState) ? pd.pumpBannerState : [])
    .filter((b) => b && b.type)
    .map((b) => ({ type: String(b.type), minutes: Number.isFinite(Number(b.timeRemaining)) ? Number(b.timeRemaining) : null }));

  const serverDataAt = Number(pd.lastMedicalDeviceDataUpdateServerTime) || Number(pd.lastConduitUpdateServerDateTime)
    || Number(pd.lastConduitUpdateServerTime) || null;
  const ai = pd.activeInsulin;
  const iobUnits = ai && Number.isFinite(Number(ai.amount)) && Number(ai.amount) >= 0 ? Number(ai.amount) : null;
  const num = (v) => (Number.isFinite(Number(v)) && v !== null && v !== '' ? Number(v) : null);

  return {
    v: 1,
    fetched_at: fetchedAtMs,
    device_data_at: serverDataAt,
    device_family: family,
    offset_ms: offset,
    last: last,
    trend_raw: pd.lastSGTrend || null,
    sgs: dedup.filter((s) => s.t >= fetchedAtMs - 24 * HOUR - 10 * MIN),
    iob: iobUnits === null ? null : { units: Math.round(iobUnits * 100) / 100, at: serverDataAt },
    alerts,
    cleared,
    bg_at: lastFingerBgAt(pd, offset),
    banners,
    pump: {
      reservoir_units: num(pd.reservoirRemainingUnits),
      reservoir_pct: num(pd.reservoirLevelPercent),
      battery_pct: num(pd.pumpBatteryLevelPercent ?? pd.medicalDeviceBatteryLevelPercent),
      suspended: pd.pumpSuspended ?? pd.medicalDeviceSuspended ?? null,
      in_range: pd.conduitMedicalDeviceInRange ?? null,
      auto_mode: pd.therapyAlgorithmState ? pd.therapyAlgorithmState.autoModeShieldState || null : null,
      system_status: pd.systemStatusMessage || null,
    },
    sensor: {
      hours_left: num(pd.sensorDurationHours),
      minutes_left: num(pd.sensorDurationMinutes),
      state: pd.sensorState || null,
      in_range: pd.conduitSensorInRange ?? null,
    },
    phone: {
      battery_pct: num(pd.conduitBatteryLevel),
      in_range: pd.conduitInRange ?? null,
    },
    carelink_stats: {
      time_in_range: num(pd.timeInRange),
      below: num(pd.belowHypoLimit),
      above: num(pd.aboveHyperLimit),
      average: num(pd.averageSG),
    },
  };
}

// ---------------------------------------------------------------- reminders
// CareLink drops a notification from the active list as soon as it is dismissed
// on the pump, even though the action (change the set, the sensor, ...) is still
// to be done. These "latches" keep such reminders on until the pump data shows
// the action was taken, or until they expire.

const LATCH_FOR = {
  set_change: 'set', sensor_change: 'sensor',
  battery_now: 'battery', battery_low: 'battery',
  reservoir_low: 'reservoir', reservoir_empty: 'reservoir',
  bg_required: 'bg',
};
export const LATCH_MAX_AGE_MS = 72 * HOUR;
const CLEARED_LOOKBACK_MS = 2 * HOUR;  // dismissed between polls; older ones were seen (or handled) already

/** Current value the "action done" check watches for each latch, or null. */
function latchMeasure(id, snap) {
  if (id === 'reservoir') return snap.pump.reservoir_units;
  if (id === 'battery') return snap.pump.battery_pct;
  if (id === 'sensor') return snap.sensor.hours_left;
  return null;
}

/** Did the pump data since the latch started show the action? */
function latchResolved(id, latch, snap) {
  const now = latchMeasure(id, snap);
  if (id === 'bg') return Boolean(snap.bg_at && snap.bg_at > latch.since);
  // A set change doesn't need a new reservoir, but its tubing is always filled.
  if (id === 'set') return Boolean(snap.set_change_at && snap.set_change_at > latch.since);
  if (now === null || latch.low === null || latch.low === undefined) return false;
  if (id === 'reservoir') return now >= latch.low + 5;                  // new reservoir filled
  if (id === 'battery') return now > latch.low;                         // new battery
  if (id === 'sensor') return now >= latch.low + 12;                    // new sensor
  return false;
}

/**
 * Update the reminder latches from a fresh snapshot.
 * latches: { [id]: { since, code, low, done? } }; done entries stop the same
 * notification (same code, trigger time within 2 min) from re-latching and are
 * dropped once they expire.
 */
export function updateLatches(prev, snap, now) {
  const out = {};
  for (const [id, l] of Object.entries(prev || {})) {
    if (now - l.since > LATCH_MAX_AGE_MS) continue;  // expired
    out[id] = { ...l };
  }
  const sources = [
    ...snap.alerts.map((a) => ({ category: a.category, code: a.code, at: a.since || now })),
    ...(snap.cleared || [])
      .filter((c) => now - c.triggered <= CLEARED_LOOKBACK_MS)
      .map((c) => ({ category: c.category, code: c.code, at: c.triggered })),
  ];
  for (const src of sources) {
    const id = LATCH_FOR[src.category];
    if (!id) continue;
    const cur = out[id];
    const same = cur && cur.code === src.code && Math.abs(cur.since - src.at) <= 2 * MIN;
    if (cur && (!cur.done || same)) continue;  // already on, or this very notification was handled
    out[id] = { since: src.at, code: src.code, low: latchMeasure(id, snap) };
  }
  for (const [id, l] of Object.entries(out)) {
    if (l.done) continue;
    const m = latchMeasure(id, snap);
    if (m !== null && (l.low === null || l.low === undefined || m < l.low)) l.low = m;
    if (latchResolved(id, l, snap)) out[id] = { ...l, done: true, done_at: now, how: 'pump data' };
  }
  return out;
}

/** Ids of reminders still waiting for the action. */
export function activeLatches(latches) {
  return Object.entries(latches || {}).filter(([, l]) => !l.done).map(([id]) => id);
}

/** The parts that matter for "did anything change" (decides whether to write KV). */
export function snapshotFingerprint(s) {
  if (!s) return '';
  return JSON.stringify([
    s.last && s.last.t, s.last && s.last.sg, s.trend_raw, s.iob && s.iob.units,
    s.alerts.map((a) => `${a.code}:${a.silenced}`), s.banners.map((b) => `${b.type}:${b.minutes}`),
    s.pump.reservoir_units, s.pump.battery_pct, s.pump.suspended, s.pump.auto_mode,
    s.sensor.hours_left, s.phone.battery_pct, s.phone.in_range,
    JSON.stringify(s.latches || {}),
  ]);
}

/** Temp target ("tijdelijke streefwaarde") from the pump banners. */
export function tempTargetFrom(snapshot) {
  const b = snapshot && snapshot.banners.find((x) => x.type === 'TEMP_TARGET');
  return b ? { active: true, minutes_left: b.minutes } : { active: false, minutes_left: null };
}
