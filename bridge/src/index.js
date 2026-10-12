// Pumpkin bridge: one JSON endpoint for the clock, fed by CareLink (pump data,
// polled by this Worker) with Nightscout as fallback for glucose and time in range.
// With Nightscout configured, each CareLink fetch is also uploaded there (see
// nightscout-upload.js), so this Worker can replace Nightscout's own CareLink poller.
// Without Nightscout it runs on CareLink alone.
//
//   GET  /clock          -> clock JSON: SugarClock custom-URL format + extra fields (see README)
//   GET  /debug          -> everything the bridge computed, for troubleshooting
//   POST /carelink/poll  -> force a CareLink fetch now (testing)
//   GET  /health         -> {"ok":true} (no auth)
//
// Authenticated routes require "Authorization: Bearer <CLOCK_TOKEN>", which is what
// the clock sends from its token field. Any doubt about the data gives a non-200,
// so the clock shows stale / no data instead of a value that might be wrong.

import { cleanEntries, computeTrend, computeTir, extractIob, nextPollSec } from './logic.js';
import { pollOnce, shouldFetch, nextDue, nextStatus } from './carelink.js';
import { mapCareLinkTrend, tempTargetFrom, activeLatches, suspendState, alertShort } from './carelink-data.js';
import { uploadEnabled, uploadToNightscout } from './nightscout-upload.js';

const MIN = 60 * 1000;
const TIR_CACHE_MS = 10 * MIN;
const TIR_CACHE_MAX_AGE_MS = 60 * MIN;
const FETCH_TIMEOUT_MS = 8000;

// Per-isolate caches. Best effort: a cold isolate just refetches.
let dayCache = { at: 0, entries: null };
let carelinkMem = { attemptAt: 0, inflight: null, lastUpload: null };

function config(env) {
  const num = (v, d) => (v === undefined || v === '' || !Number.isFinite(Number(v)) ? d : Number(v));
  return {
    nsUrl: String(env.NS_URL || '').replace(/\/+$/, ''),
    nsToken: env.NS_TOKEN || '',
    low: num(env.TARGET_LOW, 70),
    high: num(env.TARGET_HIGH, 180),
    staleMaxMin: num(env.STALE_MAX_MIN, 30),
    minCoverage: num(env.TIR_MIN_COVERAGE, 0.5),
    iobMaxAgeMin: num(env.IOB_MAX_AGE_MIN, 15),
    pollMarginSec: num(env.POLL_MARGIN_SEC, 5),
    iobEnabled: String(env.IOB_ENABLED || 'false').toLowerCase() === 'true',
    carelinkEnabled: Boolean(env.CARELINK) && String(env.CARELINK_ENABLED || 'true').toLowerCase() !== 'false',
    carelinkLagSec: num(env.CARELINK_LAG_SEC, 75),
    pumpMaxAgeMin: num(env.PUMP_MAX_AGE_MIN, 15),
    nsUpload: uploadEnabled(env),
    lang: String(env.LANG || 'en').toLowerCase() === 'nl' ? 'nl' : 'en',
  };
}

function json(body, status = 200) {
  return new Response(JSON.stringify(body), {
    status,
    headers: { 'content-type': 'application/json', 'cache-control': 'no-store' },
  });
}

function authorized(request, token) {
  if (!token) return false;
  const header = request.headers.get('authorization') || '';
  const given = header.startsWith('Bearer ') ? header.slice(7) : '';
  if (given.length !== token.length) return false;
  let diff = 0;
  for (let i = 0; i < token.length; i++) diff |= given.charCodeAt(i) ^ token.charCodeAt(i);
  return diff === 0;
}

// ---------------------------------------------------------------- Nightscout

async function nsGet(cfg, path, params = {}) {
  const url = new URL(cfg.nsUrl + path);
  for (const [k, v] of Object.entries(params)) url.searchParams.set(k, String(v));
  if (cfg.nsToken) url.searchParams.set('token', cfg.nsToken);
  const res = await fetch(url.toString(), {
    headers: { accept: 'application/json' },
    signal: AbortSignal.timeout(FETCH_TIMEOUT_MS),
  });
  if (!res.ok) throw new Error(`Nightscout ${path} returned HTTP ${res.status}`);
  return res.json();
}

async function dayEntries(cfg, nowMs) {
  if (!cfg.nsUrl) return null;
  if (dayCache.entries && nowMs - dayCache.at < TIR_CACHE_MS) return dayCache.entries;
  try {
    const raw = await nsGet(cfg, '/api/v1/entries/sgv.json', {
      count: 400,
      'find[date][$gte]': nowMs - 24 * 60 * MIN,
    });
    dayCache = { at: nowMs, entries: cleanEntries(raw) };
  } catch (e) {
    // Keep serving a recent cached result; drop it once it is too old.
    if (!dayCache.entries || nowMs - dayCache.at > TIR_CACHE_MAX_AGE_MS) dayCache = { at: 0, entries: null };
  }
  return dayCache.entries;
}

const NO_TIR = { in_range: -1, below: -1, above: -1, coverage: 0, readings: 0 };

async function fromNightscout(cfg, nowMs) {
  if (!cfg.nsUrl) return { status: 502, body: { error: 'no_glucose_source' } };
  let recent;
  try {
    recent = cleanEntries(await nsGet(cfg, '/api/v1/entries/sgv.json', { count: 12 }));
  } catch (e) {
    return { status: 502, body: { error: 'nightscout_unreachable', detail: e.message } };
  }
  const latest = recent[0];
  if (!latest) return { status: 502, body: { error: 'no_readings' } };
  const ageMin = (nowMs - latest.date) / MIN;
  if (ageMin > cfg.staleMaxMin) {
    return { status: 503, body: { error: 'stale', age_min: Math.round(ageMin), last: latest.dateString } };
  }
  const trend = computeTrend(recent);
  const day = await dayEntries(cfg, nowMs);
  return {
    status: 200,
    source: 'nightscout',
    glucose: latest.sgv,
    at: latest.date,
    trend,
    arrows: arrowsFor(trend.trend),
    tir: day ? computeTir(day, nowMs, { low: cfg.low, high: cfg.high, minCoverage: cfg.minCoverage }) : NO_TIR,
    poll: nextPollSec(recent, nowMs, { marginSec: cfg.pollMarginSec }),
    reading: { sgv: latest.sgv, date: latest.dateString, age_min: Math.round(ageMin * 10) / 10, feed_direction: latest.direction },
  };
}

// ---------------------------------------------------------------- CareLink

const ARROWS = { DoubleUp: 2, SingleUp: 1, FortyFiveUp: 1, Flat: 0, FortyFiveDown: -1, SingleDown: -1, DoubleDown: -2 };
function arrowsFor(trend) { return ARROWS[trend] ?? 0; }

async function careLinkState(env, cfg, now, { force = false, fetchImpl } = {}) {
  if (!cfg.carelinkEnabled) return { enabled: false };
  const kv = env.CARELINK;
  let [snapshot, status] = await Promise.all([
    kv.get('snapshot', { type: 'json' }),
    kv.get('status', { type: 'json' }),
  ]);
  const decision = force ? { fetch: true, reason: 'forced' }
    : shouldFetch(snapshot, status, now, carelinkMem.attemptAt, cfg.carelinkLagSec);
  let error = null;
  let polled = null;
  let upload = null;
  if (decision.fetch) {
    if (!carelinkMem.inflight) {
      carelinkMem.attemptAt = now;
      carelinkMem.inflight = pollOnce(env, now, fetchImpl || fetch).finally(() => { carelinkMem.inflight = null; });
      // Only the invocation that started the poll uploads; never throws.
      if (cfg.nsUpload) upload = carelinkMem.inflight.then((r) => uploadAfterPoll(env, r, now, fetchImpl || fetch), () => null);
    }
    try {
      polled = await carelinkMem.inflight;
      snapshot = polled.snapshot;
      const st = nextStatus(status, null, now);
      if (st) { await kv.put('status', JSON.stringify(st)); status = st; }
    } catch (e) {
      error = e;
      status = nextStatus(status, e, now);
      await kv.put('status', JSON.stringify(status));
    }
  }
  return {
    enabled: true,
    snapshot,
    status,
    decision,
    error: error ? String(error.message || error) : null,
    polled: polled ? { wrote: polled.wrote, refreshed: polled.refreshed } : null,
    upload,
  };
}

async function uploadAfterPoll(env, polled, now, fetchImpl) {
  // Nothing new and the last upload in this isolate went fine: skip the Nightscout round trip.
  if (!polled.wrote && carelinkMem.lastUpload && carelinkMem.lastUpload.ok) {
    return { at: new Date(now).toISOString(), ok: true, skipped: 'unchanged' };
  }
  let result;
  try {
    result = { at: new Date(now).toISOString(), ok: true, ...(await uploadToNightscout(env, polled, now, fetchImpl)) };
  } catch (e) {
    result = { at: new Date(now).toISOString(), ok: false, error: String(e.message || e) };
  }
  carelinkMem.lastUpload = result;
  return result;
}

function nextPollCareLink(snapshot, now, cfg) {
  const due = nextDue(snapshot, cfg.carelinkLagSec);
  let wait;
  if (due === null) wait = 60;
  else if (now < due) wait = (due - now) / 1000 + cfg.pollMarginSec;
  else {
    const late = now - due;
    wait = late < 5 * MIN ? 30 : late < 15 * MIN ? 60 : late < 60 * MIN ? 120 : 300;
  }
  return {
    next_poll_sec: Math.round(Math.min(300, Math.max(10, wait))),
    expected_at: due ? new Date(due).toISOString() : null,
    late: due !== null && now >= due,
  };
}

function fromCareLink(snapshot, cfg, now) {
  const entries = snapshot.sgs.slice().reverse().map((s) => ({ sgv: s.sg, date: s.t }));
  const mapped = mapCareLinkTrend(snapshot.trend_raw);
  const trend = mapped
    ? { trend: mapped.trend, source: 'carelink', slope: null }
    : computeTrend(entries);
  const tir = computeTir(entries, now, { low: cfg.low, high: cfg.high, minCoverage: cfg.minCoverage });
  return {
    status: 200,
    source: 'carelink',
    glucose: snapshot.last.sg,
    at: snapshot.last.t,
    trend,
    arrows: mapped ? mapped.arrows : arrowsFor(trend.trend),
    tir,
    poll: nextPollCareLink(snapshot, now, cfg),
    reading: {
      sgv: snapshot.last.sg,
      date: new Date(snapshot.last.t).toISOString(),
      age_min: Math.round(((now - snapshot.last.t) / MIN) * 10) / 10,
      feed_direction: snapshot.trend_raw,
    },
  };
}

// Clock warning ids per alert category (the clock owns how each one looks).
const WARNING_FOR = {
  delivery: 'warning', set_connect: 'warning', sensor_signal: 'warning', calibration: 'warning',
  battery_now: 'battery', battery_low: 'battery',
  reservoir_empty: 'reservoir', reservoir_low: 'reservoir',
  bg_required: 'bg',
  sensor_change: 'sensor',
  set_change: 'set',
};

const WARNING_ORDER = ['suspended', 'warning', 'battery', 'reservoir', 'bg', 'sensor', 'set'];

/**
 * Distinct clock warnings, fixed priority order: the pump's active alerts plus
 * reminders that were dismissed on the pump but not acted on yet (latches).
 * `suspend` (from suspendState): a pump stopped by hand shows the pause icon,
 * a SmartGuard auto-suspend the general warning.
 */
export function clockWarnings(alerts, latchIds = [], suspend = null) {
  const ids = new Set(latchIds);
  for (const a of alerts || []) {
    const id = WARNING_FOR[a.category];
    if (id) ids.add(id);
  }
  if (suspend === 'manual') ids.add('suspended');
  if (suspend === 'auto') ids.add('warning');
  return WARNING_ORDER.filter((id) => ids.has(id));
}

/** Pump-side extras: only from a recent snapshot, otherwise left out (null). */
function pumpExtras(snapshot, cfg, now) {
  const ref = snapshot && (snapshot.device_data_at || snapshot.fetched_at);
  const fresh = Boolean(ref) && now - ref <= cfg.pumpMaxAgeMin * MIN;
  if (!fresh) {
    return { fresh: false, iob: null, alert: '', alerts: [], warnings: [], temp_target: false, temp_target_min: null,
      sensor_hours_left: null, reservoir_units: null, pump_battery: null, phone_battery: null };
  }
  const headline = snapshot.alerts.filter((a) => a.headline);
  const tt = tempTargetFrom(snapshot);
  const iobFresh = snapshot.iob && now - (snapshot.iob.at || ref) <= cfg.iobMaxAgeMin * MIN;
  return {
    fresh: true,
    iob: iobFresh ? snapshot.iob.units : null,
    alert: headline.length ? alertShort(headline[0], cfg.lang) : '',
    warnings: clockWarnings(snapshot.alerts, activeLatches(snapshot.latches), suspendState(snapshot)),
    alerts: headline.map((a) => ({
      short: alertShort(a, cfg.lang), text: a.text, category: a.category,
      since: a.since ? Math.floor(a.since / 1000) : null, silenced: a.silenced,
    })),
    temp_target: tt.active,
    temp_target_min: tt.minutes_left,
    sensor_hours_left: snapshot.sensor.hours_left,
    reservoir_units: snapshot.pump.reservoir_units,
    pump_battery: snapshot.pump.battery_pct,
    phone_battery: snapshot.phone.battery_pct,
  };
}

const BRIDGE_ALERTS = {
  en: { login: 'CARELINK LOGIN NEEDED', failing: 'CARELINK ERROR' },
  nl: { login: 'CARELINK LOGIN NODIG', failing: 'CARELINK STORING' },
};

function bridgeAlert(cl, lang) {
  if (!cl || !cl.enabled || !cl.status || !cl.status.failures) return '';
  const texts = BRIDGE_ALERTS[lang] || BRIDGE_ALERTS.en;
  if (cl.status.auth_error) return texts.login;
  return cl.status.failures >= 3 ? texts.failing : '';
}

function careLinkDebug(cl) {
  if (!cl) return null;
  if (!cl.enabled) return { enabled: false };
  const s = cl.snapshot;
  return {
    enabled: true,
    decision: cl.decision,
    error: cl.error,
    status: cl.status,
    polled: cl.polled,
    nightscout_upload: carelinkMem.lastUpload,
    snapshot: s ? {
      fetched_at: new Date(s.fetched_at).toISOString(),
      device_data_at: s.device_data_at ? new Date(s.device_data_at).toISOString() : null,
      device_family: s.device_family,
      time_offset_h: s.offset_ms / 3600000,
      last: s.last ? { sg: s.last.sg, at: new Date(s.last.t).toISOString() } : null,
      trend_raw: s.trend_raw,
      sgs_24h: s.sgs.length,
      iob: s.iob,
      alerts: s.alerts,
      cleared: (s.cleared || []).map((c) => ({ ...c, triggered: c.triggered ? new Date(c.triggered).toISOString() : null,
        cleared_at: c.cleared_at ? new Date(c.cleared_at).toISOString() : null })),
      reminders: Object.fromEntries(Object.entries(s.latches || {}).map(([id, l]) => [id, {
        ...l, since: new Date(l.since).toISOString(), done_at: l.done_at ? new Date(l.done_at).toISOString() : undefined }])),
      banners: s.banners,
      suspended: suspendState(s),
      set_change_at: s.set_change_at ? new Date(s.set_change_at).toISOString() : null,
      pump: s.pump,
      sensor: s.sensor,
      phone: s.phone,
      carelink_stats: s.carelink_stats,
    } : null,
  };
}

// ---------------------------------------------------------------- combine

export async function buildState(env, nowMs = Date.now(), opts = {}) {
  const cfg = config(env);
  let cl = null;
  try {
    cl = await careLinkState(env, cfg, nowMs, opts);
  } catch (e) {
    cl = { enabled: true, error: `KV: ${e.message}` };
  }
  const snapshot = cl && cl.snapshot;
  const clFresh = Boolean(snapshot && snapshot.last) && (nowMs - snapshot.last.t) / MIN <= cfg.staleMaxMin;

  let base = clFresh ? fromCareLink(snapshot, cfg, nowMs) : await fromNightscout(cfg, nowMs);
  if (base.status !== 200) {
    if (snapshot && snapshot.last && !clFresh && base.body.error === 'no_glucose_source') {
      base = { status: 503, body: { error: 'stale', age_min: Math.round((nowMs - snapshot.last.t) / MIN), source: 'carelink' } };
    }
    return { ...base, upload: cl && cl.upload, debug: { ...base.body, carelink: careLinkDebug(cl) } };
  }

  // Time in range: CareLink's 24h of readings when enough, else Nightscout's.
  let tir = base.tir;
  if (base.source === 'carelink' && tir.in_range < 0) {
    const day = await dayEntries(cfg, nowMs);
    if (day) tir = computeTir(day, nowMs, { low: cfg.low, high: cfg.high, minCoverage: cfg.minCoverage });
  }

  const extras = pumpExtras(snapshot, cfg, nowMs);
  let iob = { units: extras.iob, source: extras.iob === null ? null : 'pump' };
  if (iob.units === null && cfg.iobEnabled && cfg.nsUrl) {
    try {
      const ns = extractIob(await nsGet(cfg, '/api/v2/properties/iob'), nowMs, cfg.iobMaxAgeMin);
      iob = { units: ns.units, source: ns.units === null ? null : 'nightscout', reason: ns.reason };
    } catch (e) {
      iob = { units: null, source: null, reason: e.message };
    }
  }

  const clock = {
    glucose: base.glucose,
    trend: base.trend.trend,
    timestamp: Math.floor(base.at / 1000),
    message: '',
    force_mode: -1,
    tir: tir.in_range,
    tir_below: tir.below,
    tir_above: tir.above,
    iob: iob.units,
    next_poll_sec: base.poll.next_poll_sec,
    // Extra fields for upcoming firmware; stock firmware ignores them.
    source: base.source,
    trend_arrows: base.arrows,
    alert: extras.alert,
    alerts: extras.alerts,
    warnings: extras.warnings,
    temp_target: extras.temp_target,
    temp_target_min: extras.temp_target_min,
    sensor_hours_left: extras.sensor_hours_left,
    reservoir_units: extras.reservoir_units,
    pump_battery: extras.pump_battery,
    phone_battery: extras.phone_battery,
    bridge_alert: bridgeAlert(cl, cfg.lang),
  };
  const debug = {
    clock,
    reading: base.reading,
    trend: base.trend,
    tir,
    iob,
    poll: base.poll,
    pump_data_fresh: extras.fresh,
    carelink: careLinkDebug(cl),
  };
  return { status: 200, body: clock, debug, upload: cl && cl.upload };
}

export default {
  async fetch(request, env, ctx) {
    const { pathname } = new URL(request.url);
    if (pathname === '/health') return json({ ok: true });
    const routes = { '/clock': 'GET', '/debug': 'GET', '/carelink/poll': 'POST', '/reminders/clear': 'POST' };
    if (!routes[pathname]) return json({ error: 'not_found' }, 404);
    if (request.method !== routes[pathname]) return json({ error: 'method_not_allowed' }, 405);
    if (!authorized(request, env.CLOCK_TOKEN)) return json({ error: 'unauthorized' }, 401);

    if (pathname === '/reminders/clear') return clearReminder(request, env);

    const state = await buildState(env, Date.now(), { force: pathname === '/carelink/poll' });
    if (state.upload) {
      if (pathname === '/carelink/poll') {
        const up = await state.upload;
        if (state.debug && state.debug.carelink) state.debug.carelink.nightscout_upload = up;
      } else if (ctx && ctx.waitUntil) {
        ctx.waitUntil(state.upload);
      }
    }
    if (pathname === '/clock') return json(state.body, state.status);
    return json(state.debug || state.body, state.status);
  },

  // Keep-alive: refreshes tokens and the snapshot when the clock is off.
  async scheduled(event, env, ctx) {
    ctx.waitUntil(careLinkState(env, config(env), Date.now()).then((cl) => cl.upload).catch(() => {}));
  },
};

/** POST /reminders/clear?id=set (or all): mark a reminder as done by hand. */
async function clearReminder(request, env) {
  if (!env.CARELINK) return json({ error: 'carelink_disabled' }, 400);
  const id = new URL(request.url).searchParams.get('id') || '';
  const snapshot = await env.CARELINK.get('snapshot', { type: 'json' });
  const latches = (snapshot && snapshot.latches) || {};
  const ids = id === 'all' ? Object.keys(latches).filter((k) => !latches[k].done) : [id];
  const cleared = [];
  const now = Date.now();
  for (const k of ids) {
    if (latches[k] && !latches[k].done) {
      latches[k] = { ...latches[k], done: true, done_at: now, how: 'manual' };
      cleared.push(k);
    }
  }
  if (!cleared.length) return json({ error: 'no_active_reminder', id, active: activeLatches(latches) }, 404);
  await env.CARELINK.put('snapshot', JSON.stringify({ ...snapshot, latches }));
  return json({ ok: true, cleared, active: activeLatches(latches) });
}

// Test hooks
export function _resetCache() {
  dayCache = { at: 0, entries: null };
  carelinkMem = { attemptAt: 0, inflight: null, lastUpload: null };
}
