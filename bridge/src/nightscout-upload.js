// Uploads what the bridge fetched from CareLink to Nightscout, so Nightscout keeps
// working once its own CareLink poller is off:
//   entries      sgv readings newer than Nightscout's newest
//   devicestatus pump: reservoir, battery, IOB (Nightscout's IOB plugin uses pump.iob),
//                status; plus a `carelink` block with alerts, temp target, sensor
//   treatments   boluses, carbs, finger BGs (from CareLink markers) and temp targets
//
// Stateless: each run asks Nightscout what it already has, so a failed upload is
// simply retried on the next poll. Nightscout upserts entries (sysTime+type) and
// treatments (created_at+eventType), so overlap never duplicates.

import { parseCareLinkDate, deviceTimeOffsetMs, mapCareLinkTrend, tempTargetFrom, alertShort } from './carelink-data.js';
import { computeTrend } from './logic.js';

const MIN = 60 * 1000;
const TIMEOUT_MS = 8000;
// Kept from the bridge's first versions so existing Nightscout data stays recognised.
export const DEVICE = 'sugarclock-bridge';
const TEMP_TARGET_MGDL = 150; // the 780G temp target is fixed at 150 mg/dL
const TREATMENT_OVERLAP_MS = 30 * MIN; // markers can show up late; re-send this window
const FIRST_RUN_WINDOW_MS = 15 * MIN; // no bridge treatments yet: don't backfill Nightscout's existing history

const iso = (ms) => new Date(ms).toISOString();
const num = (v) => (v === null || v === undefined || v === '' || !Number.isFinite(Number(v)) ? null : Number(v));

// ---------------------------------------------------------------- pure mapping

/** Marker fields are flat in some API versions, under data.dataValues in others. */
function field(m, key) {
  if (m[key] !== undefined) return m[key];
  const dv = m.data && m.data.dataValues;
  return dv && dv[key] !== undefined ? dv[key] : undefined;
}

function markerTime(m, offset) {
  const t = parseCareLinkDate(m.timestamp ?? m.dateTime ?? (m.data && m.data.timestamp));
  return t ? t + offset : null;
}

/** CareLink markers -> Nightscout treatments (newer than sinceMs). */
export function treatmentsFromMarkers(pd, sinceMs) {
  const offset = deviceTimeOffsetMs(pd);
  const markers = Array.isArray(pd.markers) ? pd.markers : [];
  const insulin = [];
  const meals = [];
  const out = [];
  for (const m of markers) {
    const t = markerTime(m, offset);
    if (!t || t <= sinceMs) continue;
    const type = String(m.type || '').toUpperCase();
    if (type === 'INSULIN') {
      const fast = num(field(m, 'deliveredFastAmount')) || num(field(m, 'programmedFastAmount')) || 0;
      const ext = num(field(m, 'deliveredExtendedAmount')) || 0;
      const units = Math.round((fast + ext) * 1000) / 1000;
      if (units > 0) insulin.push({ t, units, auto: String(field(m, 'activationType') || '').toUpperCase() === 'AUTOCORRECTION' });
    } else if (type === 'MEAL') {
      const carbs = num(field(m, 'amount'));
      if (carbs > 0) meals.push({ t, carbs, used: false });
    } else if (type === 'BG_READING' || type === 'CALIBRATION') {
      const bg = num(field(m, 'value')) ?? num(field(m, 'bgValue'));
      if (bg >= 20 && bg <= 600) {
        out.push({ eventType: 'BG Check', created_at: iso(t), glucose: bg, glucoseType: 'Finger', units: 'mg/dl',
          notes: type === 'CALIBRATION' ? 'Kalibratie' : undefined });
      }
    }
  }
  for (const b of insulin) {
    const meal = b.auto ? null : meals.find((x) => !x.used && Math.abs(x.t - b.t) <= 2 * MIN);
    if (meal) {
      meal.used = true;
      out.push({ eventType: 'Meal Bolus', created_at: iso(b.t), insulin: b.units, carbs: meal.carbs });
    } else {
      out.push({ eventType: 'Correction Bolus', created_at: iso(b.t), insulin: b.units, notes: b.auto ? 'Auto-correctie' : undefined });
    }
  }
  for (const m of meals) if (!m.used) out.push({ eventType: 'Carb Correction', created_at: iso(m.t), carbs: m.carbs });
  return out
    .map((x) => JSON.parse(JSON.stringify({ ...x, enteredBy: DEVICE }))) // drop undefined
    .sort((a, b) => Date.parse(a.created_at) - Date.parse(b.created_at));
}

/** Snapshot readings -> Nightscout sgv entries (newer than sinceMs). */
export function entriesFrom(snapshot, sinceMs) {
  const sgs = snapshot.sgs.filter((s) => s.t > sinceMs);
  if (!sgs.length) return [];
  const newest = sgs[sgs.length - 1];
  const mapped = mapCareLinkTrend(snapshot.trend_raw);
  let direction = mapped ? mapped.trend : null;
  if (!direction) {
    const recent = snapshot.sgs.slice().reverse().map((s) => ({ sgv: s.sg, date: s.t }));
    direction = computeTrend(recent).trend;
  }
  return sgs.map((s) => ({
    type: 'sgv',
    sgv: s.sg,
    date: s.t,
    dateString: iso(s.t),
    direction: s === newest && newest.t === snapshot.last.t ? direction : 'NONE',
    device: DEVICE,
  }));
}

/** Snapshot -> one devicestatus document in the shape Nightscout's pump/IOB plugins read. */
export function deviceStatusFrom(snapshot, lang = 'en') {
  const at = snapshot.device_data_at || snapshot.fetched_at;
  const suspended = snapshot.pump.suspended === true;
  const pump = {
    clock: iso(at),
    manufacturer: 'Medtronic',
    model: snapshot.device_family || undefined,
    reservoir: snapshot.pump.reservoir_units ?? undefined,
    battery: snapshot.pump.battery_pct !== null ? { percent: snapshot.pump.battery_pct } : undefined,
    status: { status: suspended ? 'suspended' : 'normal', suspended, bolusing: false, timestamp: iso(at) },
  };
  if (snapshot.iob) pump.iob = { timestamp: iso(snapshot.iob.at || at), iob: snapshot.iob.units, bolusiob: snapshot.iob.units };
  const doc = {
    created_at: iso(at),
    device: DEVICE,
    uploader: snapshot.phone.battery_pct !== null ? { battery: snapshot.phone.battery_pct } : undefined,
    pump,
    carelink: {
      auto_mode: snapshot.pump.auto_mode,
      system_status: snapshot.pump.system_status,
      pump_in_range: snapshot.pump.in_range,
      sensor: snapshot.sensor,
      phone_in_range: snapshot.phone.in_range,
      temp_target: tempTargetFrom(snapshot),
      alerts: snapshot.alerts.map((a) => ({
        code: a.code, category: a.category, text: a.text, short: alertShort(a, lang),
        since: a.since ? iso(a.since) : null, silenced: a.silenced,
      })),
    },
  };
  return JSON.parse(JSON.stringify(doc));
}

/**
 * Temp target sync. `latest` is the newest bridge Temporary Target in Nightscout.
 * Returns a treatment to post (start / change / cancel) or null.
 */
export function tempTargetTreatment(snapshot, latest, now) {
  const tt = tempTargetFrom(snapshot);
  const nsEnd = latest ? Date.parse(latest.created_at) + (Number(latest.duration) || 0) * MIN : 0;
  const nsActive = nsEnd > now;
  const minute = Math.floor(now / MIN) * MIN;
  if (tt.active) {
    const mins = tt.minutes_left;
    if (!(mins > 0)) return null;
    const pumpEnd = now + mins * MIN;
    if (nsActive && Math.abs(nsEnd - pumpEnd) <= 10 * MIN) return null;
    return {
      eventType: 'Temporary Target', created_at: iso(minute), duration: mins,
      targetTop: TEMP_TARGET_MGDL, targetBottom: TEMP_TARGET_MGDL, units: 'mg/dl',
      reason: 'Tijdelijke streefwaarde', enteredBy: DEVICE,
    };
  }
  if (nsActive && nsEnd - now > 2 * MIN) {
    return { eventType: 'Temporary Target', created_at: iso(minute), duration: 0, reason: 'Tijdelijke streefwaarde gestopt', enteredBy: DEVICE };
  }
  return null;
}

/**
 * Temporary raw capture (RAW_CAPTURE = "true"): the CareLink patientData as
 * received, minus the 24h of glucose readings and minus markers older than 6 h,
 * stored as its own devicestatus device. Used to find out which fields change
 * when e.g. an infusion set is changed.
 */
export const RAW_DEVICE = 'sugarclock-bridge-raw';
export function rawCaptureDoc(pd, now) {
  const { sgs, ...rest } = pd || {};
  const recent = (Array.isArray(rest.markers) ? rest.markers : []).filter((m) => {
    const t = parseCareLinkDate(m.timestamp ?? m.dateTime ?? (m.data && m.data.timestamp));
    return !t || t >= now - 6 * 60 * MIN - 3 * 60 * MIN; // + 3 h slack for pump-local times
  });
  return { created_at: iso(now), device: RAW_DEVICE, patientData: { ...rest, markers: recent, sgs_count: Array.isArray(sgs) ? sgs.length : 0 } };
}

// ---------------------------------------------------------------- I/O

/** First-upload cutoff, written to KV once (one write, ever). 0 without KV. */
async function uploadFloor(kv, now) {
  if (!kv) return 0;
  const stored = Number(await kv.get('ns_upload_since'));
  if (stored > 0) return stored;
  const floor = now - FIRST_RUN_WINDOW_MS;
  await kv.put('ns_upload_since', String(floor));
  return floor;
}

async function sha1Hex(text) {
  const buf = await crypto.subtle.digest('SHA-1', new TextEncoder().encode(text));
  return [...new Uint8Array(buf)].map((b) => b.toString(16).padStart(2, '0')).join('');
}

export function uploadEnabled(env) {
  return Boolean(env.NS_URL) && Boolean(env.NS_API_SECRET || env.NS_UPLOAD_TOKEN)
    && String(env.NS_UPLOAD || 'true').toLowerCase() !== 'false';
}

/**
 * Upload one poll's data. Returns a summary; throws on a Nightscout error so the
 * caller can report it (the next poll retries).
 */
export async function uploadToNightscout(env, { snapshot, patientData }, now, fetchImpl = fetch) {
  const base = String(env.NS_URL).replace(/\/+$/, '');
  const auth = {};
  if (env.NS_API_SECRET) auth['api-secret'] = await sha1Hex(env.NS_API_SECRET);
  // API secret (header) or an admin-role token (query); never the read-only follower token.
  const withToken = (url) => {
    if (!env.NS_API_SECRET && env.NS_UPLOAD_TOKEN) url.searchParams.set('token', env.NS_UPLOAD_TOKEN);
    return url.toString();
  };
  const get = async (path, params) => {
    const url = new URL(base + path);
    for (const [k, v] of Object.entries(params)) url.searchParams.set(k, String(v));
    const res = await fetchImpl(withToken(url), { headers: { accept: 'application/json', ...auth }, signal: AbortSignal.timeout(TIMEOUT_MS) });
    if (!res.ok) throw new Error(`Nightscout GET ${path} -> HTTP ${res.status}`);
    return res.json();
  };
  const post = async (path, body) => {
    const res = await fetchImpl(withToken(new URL(base + path)), {
      method: 'POST',
      headers: { 'content-type': 'application/json', accept: 'application/json', ...auth },
      body: JSON.stringify(body),
      signal: AbortSignal.timeout(TIMEOUT_MS),
    });
    if (!res.ok) throw new Error(`Nightscout POST ${path} -> HTTP ${res.status}`);
  };

  const [lastEntry, lastStatus, bridgeTreatments, lastTT] = await Promise.all([
    get('/api/v1/entries/sgv.json', { count: 1 }),
    get('/api/v1/devicestatus.json', { count: 1, 'find[device]': DEVICE }),
    get('/api/v1/treatments.json', { count: 20, 'find[enteredBy]': DEVICE }),
    get('/api/v1/treatments.json', { count: 1, 'find[enteredBy]': DEVICE, 'find[eventType]': 'Temporary Target' }),
  ]);

  const summary = { entries: 0, devicestatus: false, treatments: 0, temp_target: null };

  // entries
  const nsNewest = Array.isArray(lastEntry) && lastEntry[0] ? Number(lastEntry[0].date) || 0 : 0;
  const entries = snapshot.last ? entriesFrom(snapshot, nsNewest) : [];
  if (entries.length) { await post('/api/v1/entries', entries); summary.entries = entries.length; }

  // devicestatus
  const statusAt = snapshot.device_data_at || snapshot.fetched_at;
  const nsStatusAt = Array.isArray(lastStatus) && lastStatus[0] ? Date.parse(lastStatus[0].created_at) || 0 : 0;
  if (statusAt > nsStatusAt + 1000) { await post('/api/v1/devicestatus', deviceStatusFrom(snapshot, String(env.LANG || 'en').toLowerCase())); summary.devicestatus = true; }

  // treatments
  const own = (Array.isArray(bridgeTreatments) ? bridgeTreatments : []).filter((t) => t.eventType !== 'Temporary Target');
  const cursor = own.length ? Math.max(...own.map((t) => Date.parse(t.created_at) || 0)) : 0;
  // Never go back before the bridge's first upload: older treatments came from another uploader.
  const floor = await uploadFloor(env.CARELINK, now);
  const since = Math.max(cursor ? cursor - TREATMENT_OVERLAP_MS : now - FIRST_RUN_WINDOW_MS, floor);
  const ownKeys = new Set(own.map((t) => `${t.eventType}|${Date.parse(t.created_at)}`));
  const treatments = treatmentsFromMarkers(patientData || {}, since)
    .filter((t) => !ownKeys.has(`${t.eventType}|${Date.parse(t.created_at)}`));
  const tt = tempTargetTreatment(snapshot, Array.isArray(lastTT) ? lastTT[0] : null, now);
  if (tt) { treatments.push(tt); summary.temp_target = tt.duration > 0 ? 'start' : 'cancel'; }
  if (treatments.length) { await post('/api/v1/treatments', treatments); summary.treatments = treatments.length; }

  if (String(env.RAW_CAPTURE || '').toLowerCase() === 'true' && patientData) {
    await post('/api/v1/devicestatus', rawCaptureDoc(patientData, now));
    summary.raw = true;
  }

  return summary;
}
