# Pumpkin bridge: data structures

## Flow

```
                 Medtronic 780G ──BLE──> MiniMed app (patient's phone)   ──> CareLink cloud
                                                                              │
                         POST {base_cumulus}/display/message (care partner)   │
                                                                              ▼
┌────────────────────────────── Cloudflare Worker: Pumpkin bridge ──────────────────────────────┐
│                                                                                                │
│  patientData (raw, memory only)                                                                │
│      │ buildSnapshot()                     treatmentsFromMarkers()                             │
│      ▼                                          │                                              │
│  KV CARELINK ── tokens · config · snapshot · status · ns_upload_since                          │
│      │                                          │                                              │
│      │ fromCareLink() + pumpExtras()            │ uploadToNightscout() (waitUntil, background) │
│      ▼                                          ▼                                              │
│  GET /clock ──────────┐            POST entries · devicestatus · treatments                    │
│  GET /debug           │                         │                                              │
│  POST /carelink/poll  │                         │                                              │
│  cron */30 (keepalive)│                         │                                              │
└───────────────────────┼─────────────────────────┼──────────────────────────────────────────────┘
                        ▼                         ▼
                 Ulanzi TC001 (Pumpkin)      Nightscout (optional) ──> website, follower apps
                        ▲                         │
                        └── fallback: GET /api/v1/entries/sgv.json (only when CareLink data is stale)
```

**When a fetch happens:** each `/clock` request checks whether a new reading is
due (last reading + 5 min + `CARELINK_LAG_SEC`). Only then does it fetch from
CareLink, and the upload runs right after.

---

## 1. CareLink input: `patientData` (the fields used)

From `display/message`. Field names follow xDrip+ and carelink-python-client.
Not every field has been seen on every account.

```jsonc
{
  "medicalDeviceFamily": "NGP",                       // 780G
  "lastSG":   { "sg": 142, "timestamp": "2026-10-09T15:57:00" },
  "lastSGTrend": "UP",                                // NONE | UP | UP_DOUBLE | UP_TRIPLE | DOWN | DOWN_DOUBLE | DOWN_TRIPLE
  "sgs": [ { "sg": 140, "timestamp": "…", "sensorState": "NO_ERROR_MESSAGE" } ],   // 24h, every 5 min; sg 0 = no value
  "activeInsulin": { "amount": 1.35, "datetime": "…" },
  "reservoirRemainingUnits": 42.5,
  "reservoirLevelPercent": 25,
  "pumpBatteryLevelPercent": 75,
  "conduitBatteryLevel": 88,                          // phone
  "conduitInRange": true, "conduitMedicalDeviceInRange": true, "conduitSensorInRange": true,
  "sensorDurationHours": 31, "sensorState": "NO_ERROR_MESSAGE",
  "pumpSuspended": false,
  "systemStatusMessage": "NO_ERROR_MESSAGE",
  "therapyAlgorithmState": { "autoModeShieldState": "AUTO_BASAL" },
  "pumpBannerState": [ { "type": "TEMP_TARGET", "timeRemaining": 95 } ],          // tijdelijke streefwaarde
  "notificationHistory": {
    "activeNotifications": [ { "faultId": 109, "dateTime": "…", "alertSilenced": false } ]
  },
  "markers": [                                        // flat, or with values under data.dataValues
    { "type": "INSULIN", "dateTime": "…", "activationType": "AUTOCORRECTION|RECOMMENDED|MANUAL", "deliveredFastAmount": 0.4 },
    { "type": "MEAL", "dateTime": "…", "amount": 45 },
    { "type": "BG_READING", "dateTime": "…", "value": 134 }
  ],
  "lastConduitDateTime": "…",                         // device time (sometimes without a zone)
  "lastConduitUpdateServerDateTime": 1791554380000,   // server time, ms; the difference gives the hour offset
  "lastMedicalDeviceDataUpdateServerTime": 1791554380000,
  "timeInRange": 77, "belowHypoLimit": 2, "aboveHyperLimit": 21, "averageSG": 141
}
```

---

## 2. KV namespace `CARELINK`

| key | written | content |
|---|---|---|
| `tokens` | at login (you), at each refresh (~1×/h) | OAuth tokens; the refresh token rotates |
| `config` | 1× per 24h | endpoints and identity |
| `snapshot` | when data changes (~1× per reading) | compact state, see below |
| `status` | only on error or recovery | backoff state |
| `ns_upload_since` | once | cutoff for the Nightscout treatment upload |

### `tokens`
```jsonc
{
  "access_token": "eyJ…",          // JWT; exp + token_details.country are read from it
  "refresh_token": "…",            // rotates on every refresh
  "client_id": "…",
  "client_secret": "…",            // only with the non-Auth0 login
  "mag-identifier": "…",           // only with the non-Auth0 login
  "scope": "…",
  "expires_at": 1791558000000,     // added by the Worker
  "refreshed_at": 1791554400000    // added by the Worker
}
```

### `config`
```jsonc
{
  "discovery_url": "https://clcloud.minimed.eu/connect/carepartner/v13/discover/android/3.8",
  "country": "BE", "region": "EU",
  "token_url": "https://<sso-host>:443/<prefix>/<token_endpoint_path>",
  "base_carelink": "https://…/patient",            // users/me, m2m/links/patients
  "base_cumulus":  "https://…/connect/carepartner/v13",  // display/message
  "at": 1791554400000,
  "identity": { "role": "CARE_PARTNER_OUS", "care_partner": true, "username": "<cp-login>", "patient": "<patient-login>" }
}
```

### `snapshot` (the core)
All times are epoch ms (UTC), with the hour correction already applied.
```jsonc
{
  "v": 1,
  "fetched_at": 1791554400000,
  "device_data_at": 1791554380000,       // pump data freshness → PUMP_MAX_AGE_MIN
  "device_family": "NGP",
  "offset_ms": 0,                        // hour correction for zoneless times
  "last": { "t": 1791554220000, "sg": 142 },
  "trend_raw": "UP",
  "sgs": [ { "t": 1791467820000, "sg": 120 } ],   // 24h, sorted, 40–400 mg/dL
  "iob": { "units": 1.35, "at": 1791554380000 },
  "alerts": [ {
    "code": "109", "raw_fault_id": 109, "raw_message_id": null,
    "category": "set_change", "rank": 5, "headline": true,
    "text": "Set Change Reminder", "short": "SET VERVANGEN", "known": true,
    "silenced": false, "since": 1791553200000
  } ],
  "banners": [ { "type": "TEMP_TARGET", "minutes": 95 } ],
  "pump":   { "reservoir_units": 42.5, "reservoir_pct": 25, "battery_pct": 75, "suspended": false,
              "in_range": true, "auto_mode": "AUTO_BASAL", "system_status": "NO_ERROR_MESSAGE" },
  "sensor": { "hours_left": 31, "minutes_left": null, "state": "NO_ERROR_MESSAGE", "in_range": true },
  "phone":  { "battery_pct": 88, "in_range": true },
  "carelink_stats": { "time_in_range": 77, "below": 2, "above": 21, "average": 141 }
}
```

### `status`
```jsonc
{ "failures": 2, "auth_error": false, "last_error": "display/message -> HTTP 503",
  "last_error_at": 1791554400000, "next_retry_at": 1791554640000, "last_ok_at": 1791553800000 }
// after recovery: { "failures": 0, "last_ok_at": … }
```

### Alert categories, by priority (`rank`)

`short` is the Dutch text stored in the snapshot. `/clock` and Nightscout use
the English text from `alertShort()` in `carelink-data.js` unless `LANG = "nl"`.

| rank | category | short | NGP codes |
|---|---|---|---|
| 0 | delivery | TOEDIENING GESTOPT | 002 007 012 024 025 029 037 061 077 130 |
| 1 | set_connect | SET AANSLUITEN | 140 |
| 2 | battery_now | BATTERIJ VERVANGEN | 006 011 057 058 073 084 |
| 3 | reservoir_empty | RESERVOIR LEEG | 066 069 113 |
| 4 | sensor_change | SENSOR VERVANGEN | 777 794 |
| 5 | set_change | SET VERVANGEN | 109 |
| 6 | reservoir_low | RESERVOIR BIJNA LEEG | 105 |
| 7 | battery_low | BATTERIJ BIJNA LEEG | 104 |
| 8 | transmitter | ZENDER OPLADEN | 779 870 |
| 9 | sensor_signal | GEEN SENSORSIGNAAL | 780 795 796 |
| 10 | bg_required | BG METEN | 103 819 821 822 829 |
| 11 | calibration | KALIBREREN | 775 776 801 832 869 |
| 12 | meal_bolus | MAALTIJDBOLUS? | 107 |
| 13 | phone | TELEFOON BATTERIJ | (Guardian only) |
| 14 | glucose | *(no headline)* | 784 802 803 805 807 809 810 812 814–817 823 827 833 |
| 15 | info | *(no headline)* | 051 052 062 100 110 117 797 |
| 16 | other | the alert's own text | everything else |

---

## 3. Output: `GET /clock`

```jsonc
{
  // ── read by the (patched) firmware today ──
  "glucose": 142,                 // mg/dL
  "trend": "SingleUp",            // DoubleUp | SingleUp | Flat | SingleDown | DoubleDown | NONE …
  "timestamp": 1791554220,        // unix s of the reading
  "message": "",
  "force_mode": -1,
  "tir": 78, "tir_below": 4, "tir_above": 18,   // % over 24h, 70–180; -1 = not enough data
  "iob": 1.35,                    // U, pump value; null = unknown or old
  "next_poll_sec": 200,           // 10–300

  // ── Pumpkin firmware (stock SugarClock ignores these) ──
  "source": "carelink",           // carelink | nightscout
  "trend_arrows": 1,              // -3 … 3
  "alert": "CHANGE SET",          // headline in LANG, "" = none
  "alerts": [ { "short": "CHANGE SET", "text": "Set Change Reminder", "category": "set_change",
                "since": 1791553200, "silenced": false } ],
  "temp_target": true,
  "temp_target_min": 95,
  "sensor_hours_left": 31,
  "reservoir_units": 42.5,
  "pump_battery": 75,             // %
  "phone_battery": 88,            // %
  "warnings": ["warning", "battery"], // clock alert screen, highest priority first (see below)
  "bridge_alert": ""              // "" | "CARELINK LOGIN NEEDED" | "CARELINK ERROR" (LANG=nl: NODIG / STORING)
}
```

Errors (the clock shows stale or no data):

| HTTP | body |
|---|---|
| 401 | `{"error":"unauthorized"}` |
| 503 | `{"error":"stale","age_min":47,…}` |
| 502 | `{"error":"nightscout_unreachable" \| "no_readings" \| "no_glucose_source"}` |

`warnings` maps the active pump alerts to the clock's alert screen. It is
deduplicated, in priority order, and `[]` when the pump data is old.

| id | from | clock shows (5 s each) |
|---|---|---|
| `suspended` | pump stopped by hand: `pumpSuspended` or banner `DELIVERY_SUSPEND`, and no SmartGuard auto-suspend | two orange pause bars |
| `warning` | alert categories delivery, set_connect, sensor_signal, calibration; and SmartGuard auto-suspend (alerts 809/810, or a banner matching `SUSPEND…ON_LOW` / `SUSPEND…BEFORE_LOW`) | amber triangle |
| `battery` | battery_now, battery_low | light blue battery |
| `reservoir` | reservoir_empty, reservoir_low | green reservoir |
| `bg` | bg_required | red blood drop |
| `sensor` | sensor_change | `SENSOR` (narrow letters), yellow |
| `set` | set_change | `SET`, yellow |

The clock ignores ids it doesn't know.

**Reminders stay on after they're dismissed on the pump.** CareLink drops a
notification from the active list as soon as it's dismissed, even though the action
hasn't happened yet. So `battery`, `reservoir`, `bg`, `sensor` and `set` are
remembered (`snapshot.latches`) and stay in `warnings` until one of these happens:

| id | until |
|---|---|
| `set` | a tubing fill: the reservoir drops ≥ 5 U more than the recorded boluses and basal, while the pump is suspended (on 11 Oct: −9.2 U in 4 min) or, with no suspend seen, between pump readings ≤ 12 min apart with IOB up by < 0.3 U (on 14 Oct: −9.6 U, IOB 1.40 → 1.25) |
| `reservoir` | the reservoir goes up by ≥ 5 U (a new reservoir) |
| `battery` | the pump battery % goes up |
| `sensor` | sensor hours left go up by ≥ 12 (a new sensor) |
| `bg` | a finger-prick BG marker after the alert; for SmartGuard's BG required (819, 821, 822, 829) also the pump back in Auto Basal |
| any | 72 h have passed, or `POST /reminders/clear?id=<id>` (or `id=all`) |

A reminder that was dismissed between two polls is picked up from the pump's
cleared history (up to 2 h back). `suspended` and `warning` aren't remembered:
they show only while they're active on the pump.
`/debug` shows `carelink.snapshot.reminders`, `cleared` and `suspended`
(`manual`, `auto` or `null`).

All pump fields are `null` (and `alerts` is `[]`) when `device_data_at` is older
than 15 minutes.

`GET /debug` returns `{ clock, reading, trend, tir, iob, poll, pump_data_fresh, carelink: { decision, error, status, polled, nightscout_upload, snapshot } }`.

---

## 4. Output: Nightscout (optional)

`device` / `enteredBy` = `sugarclock-bridge` (name kept from the first versions). Nightscout overwrites on the keys
shown, so posting the same data twice doesn't create a duplicate.

**`/api/v1/entries`** (upsert key: `sysTime` + `type`)
```json
{ "type": "sgv", "sgv": 142, "date": 1791554220000, "dateString": "2026-10-09T13:57:00.000Z",
  "direction": "SingleUp", "device": "sugarclock-bridge" }
```

**`/api/v1/devicestatus`** (posted only when `device_data_at` is newer than the last one)
```jsonc
{
  "created_at": "2026-10-09T13:59:40.000Z",
  "device": "sugarclock-bridge",
  "uploader": { "battery": 88 },
  "pump": {                                         // read by Nightscout's pump + IOB plugins
    "clock": "…", "manufacturer": "Medtronic", "model": "NGP",
    "reservoir": 42.5,
    "battery": { "percent": 75 },
    "iob": { "timestamp": "…", "iob": 1.35, "bolusiob": 1.35 },
    "status": { "status": "normal", "suspended": false, "bolusing": false, "timestamp": "…" }
  },
  "carelink": {                                     // for the watch app and other readers
    "auto_mode": "AUTO_BASAL", "system_status": "NO_ERROR_MESSAGE", "pump_in_range": true,
    "sensor": { "hours_left": 31, "minutes_left": null, "state": "NO_ERROR_MESSAGE", "in_range": true },
    "phone_in_range": true,
    "temp_target": { "active": true, "minutes_left": 95 },
    "alerts": [ { "code": "109", "category": "set_change", "text": "Set Change Reminder", "short": "SET VERVANGEN",
                  "since": "…", "silenced": false } ]
  }
}
```

**`/api/v1/treatments`** (upsert key: `created_at` + `eventType`)

| CareLink marker | eventType | fields |
|---|---|---|
| INSULIN, `AUTOCORRECTION` | `Correction Bolus` | `insulin`, notes "Auto-correctie" |
| INSULIN + MEAL within 2 min | `Meal Bolus` | `insulin`, `carbs` |
| INSULIN, manual without carbs | `Correction Bolus` | `insulin` |
| MEAL without bolus | `Carb Correction` | `carbs` |
| BG_READING / CALIBRATION | `BG Check` | `glucose`, `glucoseType: Finger` |
| banner TEMP_TARGET starts or changes | `Temporary Target` | `duration`, `targetTop`/`targetBottom` 150, `units: mg/dl` |
| banner gone while Nightscout still shows one | `Temporary Target` | `duration: 0` (cancel) |
| AUTO_BASAL_DELIVERY | *(not uploaded)* | the micro-boluses are already in the pump's IOB |

Treatment window: from the newest bridge treatment − 30 min, never before
`ns_upload_since`.

---

## 5. Config (`wrangler.toml` + secrets)

| name | kind | default | role |
|---|---|---|---|
| `CLOCK_TOKEN` | secret | — | Bearer for `/clock`, `/debug`, `/carelink/poll` |
| `NS_TOKEN` | secret | — | Nightscout read access (fallback) |
| `NS_API_SECRET` / `NS_UPLOAD_TOKEN` | secret | — | Nightscout write access; enables the upload |
| `NS_URL` | var | empty | Nightscout address; empty = CareLink only |
| `LANG` | var | `en` | `en` / `nl`: short alert texts in `/clock` and Nightscout |
| `NS_UPLOAD` | var | `true` | upload on/off |
| `CARELINK_ENABLED` | var | `true` | CareLink on/off |
| `CARELINK_COUNTRY` | var | `BE` | region choice in discovery |
| `CARELINK_LAG_SEC` | var | `75` | reading → visible in CareLink |
| `CARELINK_USERNAME` / `CARELINK_PATIENT_USERNAME` | var | auto | identity override |
| `PUMP_MAX_AGE_MIN` | var | `15` | pump extras freshness |
| `TARGET_LOW` / `TARGET_HIGH` | var | `70` / `180` | TIR range |
| `TIR_MIN_COVERAGE` | var | `0.5` | minimum data for TIR |
| `IOB_ENABLED` / `IOB_MAX_AGE_MIN` | var | `false` / `15` | Nightscout IOB fallback |
| `POLL_MARGIN_SEC` | var | `5` | added to `next_poll_sec` |
| `STALE_MAX_MIN` | var | `30` | older → 503 |
