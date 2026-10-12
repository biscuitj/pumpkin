# Pumpkin bridge

A Cloudflare Worker that follows a Medtronic MiniMed 780G through CareLink, as a
care partner, and feeds the Pumpkin clock:

- it reads CareLink: glucose, the pump's trend and active insulin, pump alerts,
  temp target, sensor, reservoir and batteries;
- serves the clock one JSON document (`/clock`);
- optionally uploads the same data to Nightscout, so it can replace
  Nightscout's own CareLink poller. Nightscout then also gets the pump's IOB,
  reservoir and battery, boluses, carbs, finger BGs and temp targets.

Nightscout is optional. Without it the bridge runs on CareLink alone. With it,
Nightscout is also the fallback for glucose and time in range when CareLink has
a problem.

Installing it: **[../docs/INSTALL.md](../docs/INSTALL.md)**. Data structures:
[../docs/DATA-SCHEMA.md](../docs/DATA-SCHEMA.md).

```json
{"glucose":142,"trend":"SingleUp","timestamp":1791555420,"message":"","force_mode":-1,
 "tir":78,"tir_below":4,"tir_above":18,"iob":1.35,"next_poll_sec":200,
 "source":"carelink","trend_arrows":1,
 "alert":"CONNECT SET",
 "alerts":[{"short":"CONNECT SET","text":"…","category":"set_connect","since":1791554400,"silenced":false}],
 "warnings":["warning","set"],
 "temp_target":true,"temp_target_min":95,
 "sensor_hours_left":31,"reservoir_units":42.5,"pump_battery":75,"phone_battery":88,
 "bridge_alert":""}
```

The first fields follow SugarClock's custom-URL format, so stock SugarClock
firmware can show the glucose too. The Pumpkin firmware uses all of them.

## Fields

| field | meaning |
|---|---|
| `glucose`, `timestamp` | newest reading (mg/dL, unix s) |
| `trend` / `trend_arrows` | pump trend (`UP_TRIPLE` → `DoubleUp`, arrows `3`); slope-based when the pump says `NONE` |
| `tir`, `tir_below`, `tir_above` | % over the last 24h, `TARGET_LOW`–`TARGET_HIGH`; `-1` if <50% coverage |
| `iob` | pump Active Insulin (U); `null` if pump data is older than `PUMP_MAX_AGE_MIN` |
| `next_poll_sec` | when the next reading should be on CareLink (+ margin) |
| `source` | `carelink` or `nightscout` |
| `alert` | highest-priority active pump alert as short text (`LANG`), `""` if none |
| `alerts[]` | all active pump alerts, priority order; glucose alerts and info messages left out |
| `warnings` | what the clock's pump alerts screen shows, in order: `suspended`, `warning`, `battery`, `reservoir`, `bg`, `sensor`, `set` |
| `temp_target`, `temp_target_min` | temp target active + minutes left |
| `sensor_hours_left`, `reservoir_units`, `pump_battery`, `phone_battery` | as reported; `null` if unknown or old |
| `bridge_alert` | `CARELINK LOGIN NEEDED` (token rejected → redo the login) or `CARELINK ERROR` (3+ failures in a row); Dutch with `LANG = "nl"` |

Alert priority: delivery stopped › connect set › replace battery › reservoir
empty › replace sensor › change set › reservoir low › battery low › charge
transmitter › no sensor signal › check BG › calibrate › meal bolus › phone
battery › other (raw text).

## Configuration

Everything lives in `wrangler.toml` (copy `wrangler.toml.example`; it is not
committed) plus a few secrets.

| setting | kind | |
|---|---|---|
| `CLOCK_TOKEN` | secret, required | shared with the clock's token field |
| `CARELINK` | KV binding, required | tokens, account config, snapshot, status |
| `CARELINK_COUNTRY` | var | country of the CareLink account, picks the region (default `BE`) |
| `CARELINK_USERNAME`, `CARELINK_PATIENT_USERNAME` | var, optional | only when the automatic lookup fails |
| `LANG` | var | `en` or `nl`: short alert texts in `/clock` and Nightscout |
| `TARGET_LOW`, `TARGET_HIGH` | var | time-in-range limits (mg/dL) |
| `NS_URL` | var, optional | Nightscout address; empty = CareLink only |
| `NS_TOKEN` | secret, optional | Nightscout read token, for the fallback |
| `NS_API_SECRET` or `NS_UPLOAD_TOKEN` | secret, optional | write access for the upload |
| `NS_UPLOAD` | var | `true` uploads to Nightscout (with `NS_URL` and a write secret) |
| `CARELINK_ENABLED` | var | `false` = Nightscout only |

The rest (`CARELINK_LAG_SEC`, `PUMP_MAX_AGE_MIN`, `STALE_MAX_MIN`, `IOB_*`,
`POLL_MARGIN_SEC`, `RAW_CAPTURE`) is explained in `wrangler.toml.example`.

## CareLink login

The bridge needs a CareLink **care-partner** account linked to the patient, used
by nothing else. Two clients on one CareLink login log each other out.

```bash
npm install
npm run login
```

This opens Google Chrome on the CareLink login page. Log in and solve the
captcha; the tool catches the login, fetches the tokens and stores them in the
Worker's KV with Wrangler. Options: `--country NL` (default `BE`), `--us`,
`--out tokens.json` (write a file instead of storing it in KV).

The refresh token rotates and the bridge keeps it alive. If nothing refreshes it
for about a week (Worker off), CareLink drops the session: `/clock` reports
`CARELINK LOGIN NEEDED` and you run `npm run login` again.

## How it works

- **Request-driven.** `/clock` checks the stored snapshot. If a new reading is due
  (last reading + 5 min + `CARELINK_LAG_SEC`), it fetches CareLink right then,
  so KV propagation adds no delay. If the reading is late, it retries after 20 s,
  50 s, 110 s and then every ~5 min. Otherwise it refetches only after 15 min.
- **Cron `*/30`** keeps the refresh token alive when the clock is off.
- **Tokens.** The new refresh token is written to KV before anything else happens.
- **Backoff.** Failures back off 1 → 2 → 4 … 30 min; a rejected login waits
  60 min. Meanwhile `/clock` serves Nightscout, if there is one.
- **KV budget.** The snapshot is written only when something changed (~300 writes
  per day), plus token refreshes (~30). The free plan allows 1,000.
- **Fails loud.** No fresh reading anywhere → 503 (stale) or 502 (unreachable).
  The clock never shows a guess.
- **Nightscout upload** (when enabled) runs after each CareLink fetch, in the
  background, so the clock never waits for it. It asks Nightscout what it already
  has and only posts what's missing:
  - `entries`: readings newer than Nightscout's newest, filling gaps up to 24h;
  - `devicestatus`: `pump.iob`, `pump.reservoir`, `pump.battery`, `pump.status`
    in the format Nightscout's IOB and pump plugins read, plus a `carelink`
    block with alerts, temp target, sensor and phone;
  - `treatments`: auto-corrections (`Correction Bolus`), boluses with carbs
    (`Meal Bolus`), carbs alone (`Carb Correction`), finger BGs (`BG Check`) and
    the temp target (`Temporary Target`, 150 mg/dL, cancelled when it stops early).

  Everything is posted as device and enteredBy `sugarclock-bridge` (the name from
  the first versions, kept so existing data stays recognised). Treatments start
  at the first upload; there is no backfill. A Nightscout failure never touches
  `/clock`; it shows in `/debug` under `carelink.nightscout_upload`.
- **Nightscout fallback:** slope trend, adaptive polling from the upload lag, and
  Nightscout's IOB only with `IOB_ENABLED = "true"`.

## Routes

- `GET /clock`: the JSON above (Bearer `CLOCK_TOKEN`)
- `GET /debug`: everything behind it, including the CareLink snapshot summary, backoff status and poll decision
- `POST /carelink/poll`: force a CareLink fetch, returns the debug output
- `POST /reminders/clear?id=set` (`sensor`, `battery`, `reservoir`, `bg`, `all`): mark a remembered pump reminder as done by hand. The clock's "Clear pump alerts" button calls it with `id=all`
- `GET /health`: `{"ok":true}`, no auth

## Development

```bash
npm install
npm test            # node --test, no network
npx wrangler dev    # local Worker
npx wrangler deploy
```

## License

GPL-3.0 (see `LICENSE`): the alert texts in `src/alert-texts.js` come from
xDrip+'s TextMap, which is GPL-3.0. The CareLink client is written from the
public behaviour of the CareLink care-partner API, with
[carelink-python-client](https://github.com/ondrej1024/carelink-python-client)
as reference.
