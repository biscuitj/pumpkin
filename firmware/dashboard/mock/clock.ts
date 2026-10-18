/**
 * Dev-only mock of the clock's API (src/contract.ts), so the dashboard can be
 * built without firmware. Active in `npm run dev` when PUMPKIN_HOST is not set.
 *
 * Try these to see the failure paths:
 * - Wi-Fi password "wrong"         → failed_auth
 * - Wi-Fi network "nope"           → failed_no_ap
 * - join "Schoolnet"               → NTP blocked after connecting
 * - bridge URL containing "down"   → unreachable
 * - bridge URL containing "login"  → CareLink login needed
 * - bridge URL containing "stale"  → stale reading
 * - bridge URL containing "ns"     → served from Nightscout fallback
 * - clock token "bad"              → unauthorized
 *
 * The display mirror is rendered by mock/display.ts; schedule and device
 * (update, restart, reset) live in mock/schedule.ts and mock/device.ts.
 */
import type { IncomingMessage, ServerResponse } from 'node:http';
import type { Plugin } from 'vite';
import { firmwareVersion, handleDevice, isOffline } from './device.ts';
import { handleDisplay, settings as displaySettings } from './display.ts';
import { handleSchedule } from './schedule.ts';
import { Invalid, readJson, readText, send } from './http.ts';
import type {
  CheckResult,
  SourceKind,
  SourceReport,
  SourceStatus,
  SourceTest,
  SourceTestRequest,
  SourceUpdate,
  Status,
  TrialState,
  WifiJoin,
  WifiNetwork,
  WifiScan,
  WifiSecurity,
  WifiStatus,
} from '../src/contract.ts';

export function mockClock(): Plugin {
  return {
    name: 'pumpkin:mock-clock',
    apply: 'serve',
    configureServer(server) {
      server.middlewares.use((req, res, next) => {
        if (!req.url?.startsWith('/api/')) return next();
        // A little latency so loading states are visible.
        setTimeout(() => void route(req, res), 120);
      });
    },
  };
}

// ---------------------------------------------------------------- state

const now = () => Date.now();
const BOOT = now();

const NETWORKS: WifiNetwork[] = [
  { ssid: 'Thuis', rssi: -52, secure: true, enterprise: false },
  { ssid: 'Schoolnet', rssi: -67, secure: true, enterprise: true },
  { ssid: 'telenet-2C4F1', rssi: -74, secure: true, enterprise: false },
  { ssid: 'Gasten', rssi: -81, secure: false, enterprise: false },
];

const wifi = {
  ssid: 'Thuis' as string | null,
  security: 'personal' as WifiSecurity | null,
  rssi: -52,
  checksAt: now(),
  ntpBlocked: false,
  ca: { installed: false, validate: false },
};

let scanStartedAt: number | null = null;
let scanDoneAt = now() - 5 * 60_000;

let trial: {
  ssid: string;
  security: WifiSecurity;
  validateCa: boolean;
  startedAt: number;
  outcome: TrialState;
  applied: boolean;
} | null = null;

const source = {
  kind: 'bridge' as SourceKind,
  bridge_url: 'https://pumpkin-bridge.example.workers.dev/clock',
  token: 'secret',
  savedAt: now() - 60_000,
};

let test: { startedAt: number; url: string; token: string } | null = null;

// ---------------------------------------------------------------- derived

function trialState(): TrialState {
  if (!trial) return 'idle';
  const t = now() - trial.startedAt;
  const authMs = trial.security === 'enterprise' ? 3000 : 1500;
  if (t < 1200) return 'associating';
  if (t < 1200 + authMs) return 'authenticating';
  if (trial.outcome === 'connected' && !trial.applied) {
    trial.applied = true;
    wifi.ssid = trial.ssid;
    wifi.security = trial.security;
    wifi.rssi = NETWORKS.find((n) => n.ssid === trial!.ssid)?.rssi ?? -70;
    wifi.ntpBlocked = trial.ssid === 'Schoolnet';
    wifi.ca.validate = trial.validateCa;
    wifi.checksAt = now();
  }
  return trial.outcome;
}

function checks(): WifiStatus['checks'] {
  const running = now() - wifi.checksAt < 2000;
  const r = (v: CheckResult): CheckResult => (running ? 'pending' : v);
  return {
    running,
    dns: r('ok'),
    data: r(source.kind === 'bridge' && source.bridge_url.includes('down') ? 'fail' : 'ok'),
    ntp: r(wifi.ntpBlocked ? 'fail' : 'ok'),
    data_host: source.kind === 'bridge' ? hostOf(source.bridge_url) : null,
  };
}

function wifiStatus(): WifiStatus {
  const state = trialState();
  return {
    mode: state === 'associating' || state === 'authenticating' ? 'connecting' : 'connected',
    ssid: wifi.ssid,
    security: wifi.security,
    rssi: wifi.rssi + Math.round(Math.sin(now() / 4000) * 3),
    ip: '192.168.1.50',
    local_name: 'keuken.local',
    ap: null,
    ca: { ...wifi.ca },
    checks: checks(),
    trial: { state, ssid: trial?.ssid ?? null, detail: null },
  };
}

function scan(): WifiScan {
  if (scanStartedAt && now() - scanStartedAt > 2500) {
    scanStartedAt = null;
    scanDoneAt = now();
  }
  return {
    scanning: scanStartedAt !== null,
    age_sec: Math.round((now() - scanDoneAt) / 1000),
    networks: NETWORKS.map((n) => ({ ...n, rssi: n.rssi + Math.round(Math.random() * 4 - 2) })),
  };
}

function report(kind: SourceKind, url: string, token: string): SourceReport {
  const base: SourceReport = {
    state: 'ok',
    http_status: 200,
    reading_age_sec: Math.round(((now() - BOOT) / 1000 + 90) % 300),
    glucose_mgdl: Math.round(130 + 40 * Math.sin((now() - BOOT) / 600_000)),
    upstream: 'carelink',
    bridge_alert: null,
    error: null,
  };
  const none = { http_status: null, reading_age_sec: null, glucose_mgdl: null, upstream: null };

  if (kind === 'demo') return { ...base, state: 'demo', http_status: null, upstream: null };
  if (!url || !token) return { ...base, ...none, state: 'not_configured' };
  if (url.includes('down')) return { ...base, ...none, state: 'unreachable', error: 'Connection timed out' };
  if (token === 'bad') return { ...base, ...none, state: 'unauthorized', http_status: 401 };
  if (url.includes('login'))
    return { ...base, state: 'login_needed', reading_age_sec: 2460, bridge_alert: 'CARELINK LOGIN NEEDED' };
  if (url.includes('stale')) return { ...base, state: 'stale', http_status: 503, reading_age_sec: 1520 };
  if (url.includes('ns')) return { ...base, upstream: 'nightscout' };
  return base;
}

function sourceStatus(): SourceStatus {
  return {
    config: { kind: source.kind, bridge_url: source.bridge_url, token_set: source.token !== '' },
    last_fetch_age_sec: Math.round(((now() - source.savedAt) / 1000) % 60),
    ...report(source.kind, source.bridge_url, source.token),
  };
}

// ---------------------------------------------------------------- validation

function checkBridgeUrl(url: unknown): string {
  if (typeof url !== 'string' || !url.trim()) throw new Invalid('Enter the bridge address.', 'bridge_url');
  if (!/^https:\/\/[^/\s]+/.test(url.trim())) throw new Invalid('Use the https:// address of the bridge.', 'bridge_url');
  if (url.length > 255) throw new Invalid('The address is too long (255 characters max).', 'bridge_url');
  return url.trim();
}

function checkJoin(b: WifiJoin) {
  const ssidBytes = Buffer.byteLength(b?.ssid ?? '');
  if (!ssidBytes) throw new Invalid('Enter the network name.', 'ssid');
  if (ssidBytes > 32) throw new Invalid('Network names are 32 bytes at most.', 'ssid');
  if (b.security === 'personal') {
    const n = b.password.length;
    if (n && (n < 8 || n > 63)) throw new Invalid('Wi-Fi passwords are 8 to 63 characters.', 'password');
  } else if (b.security === 'enterprise') {
    if (!b.identity) throw new Invalid('Enter the username.', 'identity');
    if (!b.password) throw new Invalid('Enter the password.', 'password');
    if (b.validate_ca && !wifi.ca.installed) throw new Invalid('Upload a certificate first.', 'validate_ca');
  } else {
    throw new Invalid('Unknown security type.', 'security');
  }
}

// ---------------------------------------------------------------- routes

async function route(req: IncomingMessage, res: ServerResponse) {
  const path = (req.url ?? '').split('?')[0];
  const key = `${req.method} ${path}`;

  // A restarting clock doesn't answer at all.
  if (isOffline()) return req.socket.destroy();

  try {
    if (path.startsWith('/api/display/')) return await handleDisplay(key, req, res);
    if (path.startsWith('/api/schedule')) return await handleSchedule(key, req, res);
    if (path.startsWith('/api/device') || path.startsWith('/api/update') || path.startsWith('/api/alerts/'))
      return await handleDevice(key, req, res);

    switch (key) {
      case 'GET /api/status':
        return send(res, 200, {
          firmware_version: firmwareVersion(),
          hardware: 'Ulanzi TC001 (mock)',
          use_mmol: displaySettings.glucose.unit === 'mmol',
          uptime_sec: Math.round((now() - BOOT) / 1000),
        } satisfies Status);

      case 'GET /api/wifi':
        return send(res, 200, wifiStatus());

      case 'GET /api/wifi/networks':
        return send(res, 200, scan());

      case 'POST /api/wifi/networks/scan':
        if (scan().scanning) return send(res, 409, { error: 'A scan is already running.' });
        scanStartedAt = now();
        return send(res, 202);

      case 'POST /api/wifi/checks':
        wifi.checksAt = now();
        return send(res, 202);

      case 'POST /api/wifi/join': {
        const state = trialState();
        if (state === 'associating' || state === 'authenticating')
          return send(res, 409, { error: 'The clock is already trying a network.' });
        const b = (await readJson(req)) as WifiJoin;
        checkJoin(b);
        const outcome: TrialState =
          b.ssid === 'nope'
            ? 'failed_no_ap'
            : b.password === 'wrong'
              ? 'failed_auth'
              : 'connected';
        trial = {
          ssid: b.ssid,
          security: b.security,
          validateCa: b.security === 'enterprise' && b.validate_ca,
          startedAt: now(),
          outcome,
          applied: false,
        };
        return send(res, 202);
      }

      case 'PUT /api/wifi/ca': {
        const pem = await readText(req);
        if (pem.length > 4096) return send(res, 413, { error: 'Certificates are 4 KB at most.' });
        if (!pem.includes('-----BEGIN CERTIFICATE-----') || !pem.includes('-----END CERTIFICATE-----'))
          return send(res, 400, { error: 'That is not a PEM certificate.', field: 'ca' });
        wifi.ca.installed = true;
        return send(res, 204);
      }

      case 'DELETE /api/wifi/ca':
        wifi.ca = { installed: false, validate: false };
        return send(res, 204);

      case 'GET /api/source':
        return send(res, 200, sourceStatus());

      case 'PUT /api/source': {
        const b = (await readJson(req)) as SourceUpdate;
        if (b.kind !== 'bridge' && b.kind !== 'demo') throw new Invalid('Unknown source.', 'kind');
        if (b.kind === 'bridge') {
          const url = checkBridgeUrl(b.bridge_url);
          if (b.token !== undefined && b.token.length > 255) throw new Invalid('The token is too long.', 'token');
          if (!source.token && !b.token) throw new Invalid('Enter the clock token.', 'token');
          source.bridge_url = url;
          if (b.token) source.token = b.token;
        }
        source.kind = b.kind;
        source.savedAt = now();
        wifi.checksAt = now();
        return send(res, 200, sourceStatus());
      }

      case 'POST /api/source/test': {
        const b = (await readJson(req)) as SourceTestRequest;
        const url = checkBridgeUrl(b.bridge_url);
        const token = b.token ?? source.token;
        if (!token) throw new Invalid('Enter the clock token.', 'token');
        test = { startedAt: now(), url, token };
        return send(res, 202);
      }

      case 'GET /api/source/test': {
        if (!test) return send(res, 200, { state: 'idle', result: null } satisfies SourceTest);
        const elapsed = now() - test.startedAt;
        if (elapsed < 1400) return send(res, 200, { state: 'running', result: null } satisfies SourceTest);
        const r = report('bridge', test.url, test.token);
        return send(res, 200, {
          state: 'done',
          result: { ...r, ms: r.state === 'unreachable' ? null : 380 },
        } satisfies SourceTest);
      }

      default:
        return send(res, 404, { error: `No such endpoint: ${key}` });
    }
  } catch (e) {
    if (e instanceof Invalid) return send(res, 400, { error: e.message, field: e.field });
    return send(res, 400, { error: e instanceof Error ? e.message : 'Bad request' });
  }
}

// ---------------------------------------------------------------- helpers

function hostOf(url: string): string | null {
  try {
    return new URL(url).host;
  } catch {
    return null;
  }
}
