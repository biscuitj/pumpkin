// Shared fixtures: CareLink patientData shaped like the v13 display/message response
// (field names from xDrip+'s and carelink-python-client's models), plus an in-memory KV.

export const MIN = 60000;
export const NOW = Date.parse('2026-10-09T14:00:00Z'); // 16:00 in Brussels

const pad = (n) => String(n).padStart(2, '0');
function iso(ms, { zone = true, offsetH = 0 } = {}) {
  const d = new Date(ms + offsetH * 3600000);
  const s = `${d.getUTCFullYear()}-${pad(d.getUTCMonth() + 1)}-${pad(d.getUTCDate())}T${pad(d.getUTCHours())}:${pad(d.getUTCMinutes())}:${pad(d.getUTCSeconds())}`;
  return zone ? `${s}.000Z` : s;
}

/**
 * patientData with a reading every 5 min for 24h, newest `lastAgeMin` old.
 * localTimes: timestamps without zone in Brussels time (+2h), as some responses do.
 */
export function patientData({ lastAgeMin = 3, localTimes = false, values, trend = 'NONE', extra = {} } = {}) {
  const t = (ms) => iso(ms, localTimes ? { zone: false, offsetH: 2 } : {});
  const last = NOW - lastAgeMin * MIN;
  const sgs = [];
  for (let i = 287; i >= 0; i--) {
    const at = last - i * 5 * MIN;
    const sg = values ? values(i) : 120;
    sgs.push({ sg, timestamp: t(at), sensorState: 'NO_ERROR_MESSAGE', kind: 'SG', version: 1 });
  }
  sgs.push({ sg: 0, timestamp: t(last - 3 * MIN), sensorState: 'WARM_UP' }); // filtered: no value
  return {
    medicalDeviceFamily: 'NGP',
    lastConduitDateTime: t(NOW - 30000),
    lastConduitUpdateServerDateTime: NOW - 20000,
    lastMedicalDeviceDataUpdateServerTime: NOW - 20000,
    lastSGTrend: trend,
    lastSG: { sg: sgs[287].sg, timestamp: sgs[287].timestamp },
    sgs,
    activeInsulin: { amount: 1.35, datetime: t(NOW - MIN), precision: 'MAX', kind: 'Insulin' },
    reservoirRemainingUnits: 42.5,
    reservoirLevelPercent: 25,
    pumpBatteryLevelPercent: 75,
    conduitBatteryLevel: 88,
    conduitInRange: true,
    conduitMedicalDeviceInRange: true,
    conduitSensorInRange: true,
    sensorDurationHours: 31,
    sensorState: 'NO_ERROR_MESSAGE',
    pumpSuspended: false,
    systemStatusMessage: 'NO_ERROR_MESSAGE',
    therapyAlgorithmState: { autoModeShieldState: 'AUTO_BASAL' },
    pumpBannerState: [],
    notificationHistory: { activeNotifications: [], clearedNotifications: [] },
    timeInRange: 77, belowHypoLimit: 2, aboveHyperLimit: 21, averageSG: 141,
    ...extra,
  };
}

export function memoryKV(initial = {}) {
  const store = new Map(Object.entries(initial).map(([k, v]) => [k, typeof v === 'string' ? v : JSON.stringify(v)]));
  const kv = {
    store,
    puts: [],
    async get(key, opts) {
      const v = store.get(key);
      if (v === undefined) return null;
      return opts && opts.type === 'json' ? JSON.parse(v) : v;
    },
    async put(key, value) { kv.puts.push(key); store.set(key, value); },
  };
  return kv;
}

/** A JWT-shaped access token (unsigned) with exp and the token_details CareLink uses. */
export function jwt(expMs, details = { country: 'BE', preferred_username: 'cp-user' }, iatMs) {
  const b64 = (o) => btoa(JSON.stringify(o)).replace(/=+$/, '').replace(/\+/g, '-').replace(/\//g, '_');
  const payload = { exp: Math.floor(expMs / 1000), token_details: details };
  if (iatMs) payload.iat = Math.floor(iatMs / 1000);
  return `${b64({ alg: 'none' })}.${b64(payload)}.sig`;
}

/** Fake CareLink cloud: discovery, SSO config, token endpoint and API. */
export function fakeCareLink({ data = () => patientData(), refreshStatus = 200, refreshBody = { error: 'invalid_grant', error_description: 'Unknown or invalid refresh token.' }, displayStatuses = [], patients = [{ username: 'patient-a' }] } = {}) {
  const calls = [];
  let refreshCount = 0;
  const statuses = [...displayStatuses];
  const reply = (body, status = 200) => new Response(JSON.stringify(body), { status, headers: { 'content-type': 'application/json' } });
  const handler = async (url, init = {}) => {
    const u = new URL(url);
    const body = init.body;
    calls.push({ url: u, method: init.method || 'GET', headers: init.headers || {}, body });
    if (u.pathname.includes('/discover/')) {
      return reply({
        supportedCountries: [{ US: { region: 'US' } }, { BE: { region: 'EU' } }],
        CP: [
          { region: 'US', UseSSOConfiguration: 'SSOConfiguration', SSOConfiguration: 'https://sso.example/us.json', baseUrlCareLink: 'https://carelink.example/us', baseUrlCumulus: 'https://cumulus.example/us' },
          { region: 'EU', UseSSOConfiguration: 'Auth0SSOConfiguration', Auth0SSOConfiguration: 'https://sso.example/eu.json', baseUrlCareLink: 'https://carelink.example/patient', baseUrlCumulus: 'https://cumulus.example/connect/carepartner/v13' },
        ],
      });
    }
    if (u.href === 'https://sso.example/eu.json') {
      return reply({ server: { hostname: 'auth.example', port: 443, prefix: '' }, system_endpoints: { token_endpoint_path: '/oauth/token' } });
    }
    if (u.href === 'https://auth.example:443/oauth/token' || u.href === 'https://auth.example/oauth/token') {
      if (refreshStatus !== 200) {
        if (typeof refreshBody === 'string') return new Response(refreshBody, { status: refreshStatus, headers: { 'content-type': 'text/html' } });
        return reply(refreshBody, refreshStatus);
      }
      refreshCount++;
      return reply({ access_token: jwt(Date.parse('2026-10-09T15:00:00Z')), refresh_token: `rt-${refreshCount}`, expires_in: 3600 });
    }
    if (u.pathname === '/patient/users/me') return reply({ role: 'CARE_PARTNER_OUS' });
    if (u.pathname === '/patient/users/me/profile') return reply({ username: 'cp-user' });
    if (u.pathname === '/patient/m2m/links/patients') return reply(patients);
    if (u.pathname.endsWith('/display/message')) {
      const st = statuses.length ? statuses.shift() : 200;
      if (st !== 200) return reply({ error: 'nope' }, st);
      return reply({ metadata: {}, patientData: data() });
    }
    return reply({ error: 'unexpected ' + u.href }, 404);
  };
  return { handler, calls, get refreshCount() { return refreshCount; } };
}
