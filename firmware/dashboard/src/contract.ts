/**
 * Pumpkin clock HTTP API, v1: the contract between dashboard and firmware.
 *
 * The dashboard is built against this file and the dev mock (mock/clock.ts);
 * the firmware implements it. Conventions:
 * - JSON in and out, snake_case fields, `null` for "unknown", never sentinel numbers.
 * - Errors: non-2xx with `{ "error": string, "field"?: string }`. `field` names the
 *   request field at fault so the dashboard can put the message next to it.
 * - Anything that takes time (scans, joins, tests) answers 202 and is followed by
 *   polling the matching GET. No handler blocks on the network.
 * - Ages are relative seconds, not timestamps: the clock may not have the time yet.
 * - Secrets (Wi-Fi passwords, the clock token) are write-only and never returned.
 */

// ---------------------------------------------------------------- status

/** GET /api/status: small, polled every few seconds by every page. */
export interface Status {
  firmware_version: string;
  hardware?: string;
  /** Mirrors DisplaySettings.glucose.unit, for pages that only need the unit. */
  use_mmol?: boolean;
  uptime_sec: number;
}

// ---------------------------------------------------------------- Wi-Fi

export type WifiMode = 'connected' | 'connecting' | 'disconnected' | 'setup_ap';
export type WifiSecurity = 'personal' | 'enterprise';
export type EapMethod = 'peap' | 'ttls';
export type CheckResult = 'pending' | 'ok' | 'fail' | 'skipped';
export type TrialState =
  | 'idle'
  | 'associating'
  | 'authenticating'
  | 'connected'
  | 'failed_auth'
  | 'failed_no_ap'
  | 'failed_timeout';

/** GET /api/wifi */
export interface WifiStatus {
  mode: WifiMode;
  ssid: string | null;
  security: WifiSecurity | null;
  rssi: number | null;
  ip: string | null;
  /** mDNS name, e.g. "keuken.local" */
  local_name: string | null;
  /** Set while the clock runs its setup hotspot. */
  ap: { ssid: string; ip: string; stations: number } | null;
  /** Enterprise server certificate (stored on the clock). */
  ca: { installed: boolean; validate: boolean };
  checks: NetChecks;
  /** Last join attempt started from the dashboard or setup portal. */
  trial: WifiTrial;
}

/** Probes run after every connection; tell a school IT person what to allow. */
export interface NetChecks {
  running: boolean;
  dns: CheckResult;
  /** HTTPS to the data source host */
  data: CheckResult;
  /** UDP 123 */
  ntp: CheckResult;
  data_host: string | null;
}

export interface WifiTrial {
  state: TrialState;
  ssid: string | null;
  /** Extra detail from the Wi-Fi stack, e.g. an EAP failure reason. */
  detail: string | null;
}

/** GET /api/wifi/networks: result of the last scan. */
export interface WifiScan {
  scanning: boolean;
  age_sec: number | null;
  networks: WifiNetwork[];
}

export interface WifiNetwork {
  ssid: string;
  rssi: number;
  secure: boolean;
  enterprise: boolean;
}

// POST /api/wifi/networks/scan  → 202 (409 while a scan runs)
// POST /api/wifi/checks         → 202, re-runs NetChecks

/** POST /api/wifi/join → 202 (409 while a join runs). Saved only if the join works. */
export type WifiJoin =
  | { ssid: string; security: 'personal'; password: string }
  | {
      ssid: string;
      security: 'enterprise';
      eap: EapMethod;
      identity: string;
      password: string;
      anon_identity: string;
      validate_ca: boolean;
    };

// PUT    /api/wifi/ca  text/plain PEM, max 4 KB → 204
// DELETE /api/wifi/ca                           → 204 (also turns validate off)

// ---------------------------------------------------------------- data source

/**
 * Where readings come from. v1: the Pumpkin bridge (CareLink care-partner login
 * on a Cloudflare Worker) or demo data. Room for 'carelink' (on-device) later.
 */
export type SourceKind = 'bridge' | 'demo';

export type SourceState =
  | 'not_configured' // bridge chosen but no address or token
  | 'waiting' // configured, no fetch finished yet
  | 'ok' // bridge answered with a fresh reading
  | 'stale' // bridge answered, reading older than the stale limit (or bridge 503)
  | 'login_needed' // bridge_alert: CareLink login expired
  | 'carelink_error' // bridge_alert: CareLink failing repeatedly
  | 'unauthorized' // 401/403: clock token refused
  | 'unreachable' // DNS, TLS, timeout or 5xx other than 503
  | 'demo';

export interface SourceConfig {
  kind: SourceKind;
  /** The bridge's /clock address, https only. */
  bridge_url: string;
  token_set: boolean;
}

/** GET /api/source */
export interface SourceStatus extends SourceReport {
  config: SourceConfig;
  last_fetch_age_sec: number | null;
}

/** What one fetch from the bridge told us; shared by status and tests. */
export interface SourceReport {
  state: SourceState;
  http_status: number | null;
  reading_age_sec: number | null;
  glucose_mgdl: number | null;
  /** Which upstream the bridge used: 'nightscout' means it fell back. */
  upstream: 'carelink' | 'nightscout' | null;
  /** Text as the bridge sent it (may be Dutch). */
  bridge_alert: string | null;
  /** Transport error detail, e.g. "TLS handshake failed". */
  error: string | null;
}

/** PUT /api/source → 200 SourceStatus. Omit `token` to keep the saved one. */
export interface SourceUpdate {
  kind: SourceKind;
  bridge_url?: string;
  token?: string;
}

/** POST /api/source/test → 202. Omit `token` to test with the saved one. */
export interface SourceTestRequest {
  bridge_url: string;
  token?: string;
}

/** GET /api/source/test */
export interface SourceTest {
  state: 'idle' | 'running' | 'done';
  result: (SourceReport & { ms: number | null }) | null;
}

// ---------------------------------------------------------------- display

/** Screens in rotation order. Glucose is always on; the rest can be switched off. */
export const SCREENS = ['glucose', 'tir', 'iob', 'alerts', 'time', 'pet'] as const;
export type ScreenId = (typeof SCREENS)[number];

/** Pumpkin's pets. Ids are names, never numbers, so pets can be added or removed freely. */
export const PETS = [
  { id: 'patch', name: 'Patch', kind: 'pumpkin' },
  { id: 'toffee', name: 'Toffee', kind: 'cat' },
  { id: 'kiko', name: 'Kiko', kind: 'frog' },
  { id: 'domino', name: 'Domino', kind: 'penguin' },
  { id: 'hoot', name: 'Hoot', kind: 'owl' },
  { id: 'leo', name: 'Leo', kind: 'lion' },
  { id: 'stella', name: 'Stella', kind: 'unicorn' },
  { id: 'kobi', name: 'Kobi', kind: 'crab' },
] as const;
export type PetId = (typeof PETS)[number]['id'];

export type ColorPreset = 'classic' | 'colorblind' | 'calm';
export type GlucoseBand = 'urgent_low' | 'low' | 'in_range' | 'high' | 'urgent_high' | 'stale';

/**
 * Range colours as RGB hex, before brightness. The firmware uses this exact table;
 * the pet's LOW / OK / HIGH follow the low, in_range and high colours.
 */
export const COLOR_PRESETS: Record<ColorPreset, { name: string; colors: Record<GlucoseBand, string> }> = {
  classic: {
    name: 'Classic',
    colors: {
      urgent_low: '#ea4335',
      low: '#fbbc04',
      in_range: '#34a853',
      high: '#fbbc04',
      urgent_high: '#ea4335',
      stale: '#808080',
    },
  },
  colorblind: {
    // Low is warm, high is cool, in range is blue: no red/green pair to tell apart.
    name: 'Colour-blind friendly',
    colors: {
      urgent_low: '#ff3b1f',
      low: '#ff9f00',
      in_range: '#2d9cff',
      high: '#b57bff',
      urgent_high: '#e040fb',
      stale: '#808080',
    },
  },
  calm: {
    // Only out-of-range readings get colour.
    name: 'Calm',
    colors: {
      urgent_low: '#ff3020',
      low: '#ffb000',
      in_range: '#e8e8e8',
      high: '#ffb000',
      urgent_high: '#ff3020',
      stale: '#606060',
    },
  },
};

/** Limits the firmware enforces; the dashboard validates against the same numbers. */
export const DISPLAY_LIMITS = {
  glucose_mgdl: { min: 40, max: 400 },
  stale_after_min: { min: 10, max: 60 },
  rotation_seconds: { min: 3, max: 300 },
  brightness: { min: 1, max: 255 },
} as const;

/** GET /api/display/settings */
export interface DisplaySettings {
  /** Which optional screens are in the rotation. Alerts only show while there is one. */
  screens: { tir: boolean; iob: boolean; alerts: boolean; time: boolean; pet: boolean };
  /** auto: change screen every `seconds`; otherwise only the buttons do. */
  rotation: { auto: boolean; seconds: number };
  glucose: {
    unit: 'mgdl' | 'mmol';
    /** Thresholds, always in mg/dL: urgent_low < low < high < urgent_high. */
    urgent_low: number;
    low: number;
    high: number;
    urgent_high: number;
    /** Grey after this; dimmed with "!" from half of it. */
    stale_after_min: number;
    /** While a fresh reading is below `low`: glucose screen only, buttons and rotation paused. */
    low_lock: boolean;
    /** Show the change for 3 s when a new reading arrives. */
    delta_flash: boolean;
    colors: ColorPreset;
  };
  time: { hour24: boolean; date: 'off' | 'day_month' | 'month_day' };
  pet: {
    id: PetId;
    style: 'text' | 'icon' | 'pet_only';
    /** Text style only: time in range instead of LOW / OK / HIGH. */
    show_tir: boolean;
    /** Halloween, New Year. */
    seasonal: boolean;
  };
  /** Pump alerts take the pet's place in the rotation instead of following it. */
  alerts: { replace_pet: boolean };
  /** auto: light sensor; otherwise `level`. */
  brightness: { auto: boolean; level: number };
}

/**
 * PATCH /api/display/settings → 200 DisplaySettings. Send only what changed, one
 * level deep: `{ "glucose": { "low": 75 } }`. Errors name the field as
 * "group.key", e.g. "glucose.low". Changes apply at once.
 */
export type DisplayPatch = { [K in keyof DisplaySettings]?: Partial<DisplaySettings[K]> };

/**
 * GET /api/display/frame: what the matrix shows, 768 bytes of RGB (32×8, row by
 * row, before brightness), `application/octet-stream`.
 * - Header `X-Screen`: a ScreenId, or 'status' / 'boot' / 'update'.
 * - ETag + If-None-Match → 304 when unchanged; 204 while no frame is ready yet.
 * - The firmware only captures frames while someone asks for them.
 *
 * POST /api/display/next, /api/display/prev → 204 (ignored during the low lock).
 */

/**
 * POST /api/display/preview → 202: shows a screen for 8 s; alerts play each alert
 * once (5 s each). Never over an urgent low or during the low lock.
 */
export type DisplayPreview =
  | { screen: 'pet'; pet: PetId; range: 'low' | 'in_range' | 'high' }
  | { screen: 'alerts' }
  | { screen: 'glucose'; mgdl: number };

/**
 * GET /api/display/pets → what each pet looks like, for the picker. Drawn by
 * the clock's own pet code: the in-range pose, cropped to the pet, two frames
 * that alternate every `frame_ms`.
 */
export interface PetSprites {
  frame_ms: number;
  /** Palette key → '#rrggbb'. */
  colors: Record<string, string>;
  /** Each frame: 8 rows of equal length, one palette key per pixel, '.' = off. */
  pets: { id: PetId; frames: string[][] }[];
}

/**
 * POST /api/alerts/clear → 202: the clock asks the bridge to forget remembered
 * pump alerts (`/reminders/clear?id=all`) from its network task, then fetches.
 * GET /api/alerts/clear → progress of the last request.
 */
export interface AlertsClear {
  state: 'idle' | 'running' | 'done' | 'failed';
  error: string | null;
}

// ---------------------------------------------------------------- schedule

export const WEEKDAYS = ['mon', 'tue', 'wed', 'thu', 'fri', 'sat', 'sun'] as const;
export type Weekday = (typeof WEEKDAYS)[number];

/** Minutes since local midnight, 0–1440. `to` 1440 means "until midnight". */
export interface AwakeHours {
  from: number;
  to: number;
}

/**
 * When the clock is awake. Outside those hours it's in night mode: dimmed, or
 * black and silent (`mute`; nothing wakes it, not even an urgent low). The pet
 * sleeps outside awake hours too. Fetching never stops.
 */
export interface ScheduleSettings {
  /** false: awake around the clock. */
  enabled: boolean;
  /** Per day; null = asleep all day (e.g. weekends for a school clock). from < to. */
  awake: Record<Weekday, AwakeHours | null>;
  night: 'dim' | 'mute';
  /** Brightness while dimmed, 1–255. */
  night_brightness: number;
}

/** GET /api/schedule */
export interface ScheduleStatus {
  settings: ScheduleSettings;
  now: {
    /** false until NTP or the RTC gave a time; the schedule is ignored until then. */
    time_valid: boolean;
    weekday: Weekday | null;
    /** Minutes since local midnight. */
    minute: number | null;
    state: 'awake' | 'night' | 'off';
    /** Next switch between awake and night, null if none in the coming week. */
    next_change: { weekday: Weekday; minute: number; to: 'awake' | 'night' } | null;
  };
}

/** PATCH /api/schedule → 200 ScheduleStatus. Top-level keys; `awake` merges per day. */
export type SchedulePatch = Partial<Omit<ScheduleSettings, 'awake'>> & {
  awake?: Partial<Record<Weekday, AwakeHours | null>>;
};

// ---------------------------------------------------------------- device

export type ResetReason = 'power_on' | 'software' | 'watchdog' | 'panic' | 'brownout' | 'other';
export type UpdateError =
  | 'not_firmware' // doesn't start like an ESP32 app image
  | 'wrong_chip' // ESP32-S2/S3/C3… image
  | 'not_pumpkin' // ESP32 image without the Pumpkin marker
  | 'wrong_hardware' // Pumpkin, built for other hardware
  | 'truncated'
  | 'write_failed'
  | 'busy';

/** GET /api/device */
export interface DeviceInfo {
  /** Display name, ≤ 32 characters; the `.local` name is derived from it. */
  name: string;
  local_name: string | null;
  password_set: boolean;
  firmware: {
    version: string;
    hardware: string;
    /** Set once after an update that failed its first boot and was rolled back. */
    rolled_back: { from_version: string; reason: string } | null;
  };
  time: {
    /** IANA id as chosen in the dashboard, e.g. "Europe/Brussels". */
    zone: string;
    synced: boolean;
    /** 'http': from the bridge's Date header, when the network blocks NTP. */
    source: 'ntp' | 'http' | 'rtc' | 'none';
    /** Local "YYYY-MM-DDTHH:MM", null without a valid time. */
    local: string | null;
  };
  diagnostics: {
    uptime_sec: number;
    reset_reason: ResetReason;
    free_heap: number;
    min_free_heap: number;
    largest_free_block: number;
    /** Light sensor, raw 0–4095. */
    light: number;
    /** Brightness the matrix uses right now, 0–255. */
    brightness_now: number;
    buttons: { left: boolean; middle: boolean; right: boolean };
    last_fetch_error: string | null;
  };
}

/**
 * PATCH /api/device → 200 DeviceInfo. The clock doesn't carry a time zone
 * database: the dashboard sends the POSIX rule with the zone id.
 */
export interface DevicePatch {
  name?: string;
  time_zone?: { zone: string; posix: string };
}

// PUT  /api/device/password { "password": string } → 204. "" removes it.
//      HTTP Basic, user "pumpkin"; the setup portal's probes stay open.
// POST /api/device/restart       → 202, restarts after answering
// POST /api/device/factory-reset { "confirm": "erase" } → 202; back to the setup hotspot

/**
 * POST /api/update, body: the .bin as application/octet-stream → 202 when
 * received and checked; 400 { error, field: UpdateError } when refused.
 * The clock checks while it streams (magic byte, chip, Pumpkin marker) and keeps
 * running its current firmware on any failure. Then it restarts into the new
 * one; GET /api/status shows the new version once it's back.
 */
export interface UpdateStatus {
  state: 'idle' | 'receiving' | 'restarting' | 'failed';
  received: number;
  error: UpdateError | null;
}

/** The marker every Pumpkin image carries; the dashboard checks for it before uploading. */
export const IMAGE_MARKER = 'PUMPKIN-IMAGE|ulanzi-tc001-esp32-4mb|';

/** Error body for every non-2xx answer. */
export interface ApiErrorBody {
  error: string;
  field?: string;
}
