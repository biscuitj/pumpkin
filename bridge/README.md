# SugarClock bridge

Cloudflare Worker that is the only CareLink client:

- it reads CareLink as a care partner (glucose, the pump's trend and active
  insulin, pump alerts, temp target);
- serves the SugarClock one JSON document (`/clock`);
- uploads the same data to Nightscout (ns10be), which replaces ns10be's own
  CareLink poller. Nightscout keeps its graphs, follower apps and history, and now
  also gets pump IOB, reservoir and battery, boluses, carbs, finger BGs and temp
  targets.

Nightscout stays the fallback for glucose and time in range if KV or CareLink
has a problem.

```json
{"glucose":142,"trend":"SingleUp","timestamp":1791555420,"message":"","force_mode":-1,
 "tir":78,"tir_below":4,"tir_above":18,"iob":1.35,"next_poll_sec":200,
 "source":"carelink","trend_arrows":1,
 "alert":"SET AANSLUITEN",
 "alerts":[{"short":"SET AANSLUITEN","text":"…","category":"set_connect","since":1791554400,"silenced":false}],
 "warnings":["warning","set"],
 "temp_target":true,"temp_target_min":95,
 "sensor_hours_left":31,"reservoir_units":42.5,"pump_battery":75,"phone_battery":88,
 "bridge_alert":""}
```

The first ten fields are what (patched) SugarClock firmware reads today. Stock
firmware ignores everything else. The rest is ready for the next firmware round.

## Fields

| field | meaning |
|---|---|
| `glucose`, `timestamp` | newest reading (mg/dL, unix s) |
| `trend` / `trend_arrows` | pump trend (`UP_TRIPLE` → `DoubleUp`, arrows `3`); slope-based when the pump says `NONE` |
| `tir`, `tir_below`, `tir_above` | % over the last 24h, `TARGET_LOW`–`TARGET_HIGH`; `-1` if <50% coverage |
| `iob` | pump Active Insulin (U); `null` if pump data is older than `PUMP_MAX_AGE_MIN` |
| `next_poll_sec` | when the next reading should be on CareLink (+ margin) |
| `source` | `carelink` or `nightscout` |
| `alert` | highest-priority active pump alert as short Dutch text, `""` if none |
| `alerts[]` | all active pump alerts, priority order; glucose alerts and info messages left out |
| `warnings` | what the clock's pump alerts screen shows, in order: `suspended`, `warning`, `battery`, `reservoir`, `bg`, `sensor`, `set` (see ../docs/DATA-SCHEMA.md) |
| `temp_target`, `temp_target_min` | tijdelijke streefwaarde active + minutes left |
| `sensor_hours_left`, `reservoir_units`, `pump_battery`, `phone_battery` | as reported; `null` if unknown or old |
| `bridge_alert` | `CARELINK LOGIN NODIG` (token rejected → redo the login) or `CARELINK STORING` (3+ failures in a row) |

Alert priority: toediening gestopt › set aansluiten › batterij vervangen ›
reservoir leeg › sensor vervangen › set vervangen › reservoir bijna leeg ›
batterij bijna leeg › zender opladen › geen sensorsignaal › BG meten ›
kalibreren › maaltijdbolus › telefoon batterij › other (raw text). Alert texts
come from xDrip+'s TextMap (GPL-3.0).

## How it works

- **Request-driven.** `/clock` checks the stored snapshot. If a new reading is due
  (last reading + 5 min + `CARELINK_LAG_SEC`), it fetches CareLink right then,
  so KV propagation adds no delay. If the reading is late, it retries after 20 s,
  50 s, 110 s and then every ~5 min. Otherwise it refetches only after 15 min.
- **Cron `*/30`** keeps the refresh token alive when the clock is off.
- **Tokens.** The refresh token rotates. The new one is written to KV before
  anything else happens. If no refresh happens for about a week, CareLink drops
  the session and you redo the login.
- **Backoff.** Failures back off 1 → 2 → 4 … 30 min; a rejected login waits
  60 min. Meanwhile `/clock` serves Nightscout.
- **KV budget.** The snapshot is written only when something changed (~300 writes
  per day), plus token refreshes (~30). The free plan allows 1,000. The upload
  needs no KV beyond one key written once.
- **Nightscout upload.** Runs after each CareLink fetch, in the background, so
  the clock never waits for it. It asks Nightscout what it already has and only
  posts what's missing:
  - `entries`: readings newer than Nightscout's newest. This also fills a gap
    after an outage, up to 24h.
  - `devicestatus`: `pump.iob`, `pump.reservoir`, `pump.battery` and
    `pump.status`, in the format Nightscout's IOB and pump plugins read, so its
    IOB becomes the pump's own value. A `carelink` block adds alerts, the temp
    target, sensor and phone.
  - `treatments`:
    - auto-corrections become `Correction Bolus`;
    - a bolus with carbs becomes `Meal Bolus`;
    - carbs alone become `Carb Correction`;
    - finger BGs become `BG Check`;
    - the tijdelijke streefwaarde becomes `Temporary Target` (150 mg/dL, with a
      cancel when it stops early).

  Everything is posted as device and enteredBy `sugarclock-bridge`. There is no
  history backfill: treatments start at the first upload. A Nightscout failure
  never touches `/clock`; it shows in `/debug` under
  `carelink.nightscout_upload`, and the next poll retries.
- **Fails loud.** No fresh reading anywhere → 503 (stale) or 502 (unreachable).
  The clock never shows a guess.
- **Nightscout fallback** works as before: slope trend, adaptive polling from the
  ObjectId upload lag, and IOB only with `IOB_ENABLED = "true"`.

## Setup: CareLink + Nightscout upload

Step-by-step version with checks and troubleshooting: **../docs/INSTALL.md**. Data structures: **../docs/DATA-SCHEMA.md**.

Use ns10be's dedicated care-partner account. Two clients on one CareLink login
break each other, so the switchover goes in this order:

1. **KV namespace**
   ```bash
   npx wrangler kv namespace create CARELINK
   ```
   Paste the printed `id` into `wrangler.toml` in place of `REPLACE_WITH_KV_NAMESPACE_ID`.

2. **Nightscout secret.** Use ns10be's `API_SECRET`, from its settings:
   ```bash
   npx wrangler secret put NS_API_SECRET
   ```
   Instead, you can create a token with the `admin` role in Nightscout
   (Admin tools → Subjects) and use `npx wrangler secret put NS_UPLOAD_TOKEN`.

3. **Turn off ns10be's CareLink poller.** From here until step 6, Nightscout gets
   no data. That's a few minutes; step 6 fills the gap in the readings.

4. **One-time CareLink login** (needs a real browser because of the reCAPTCHA).
   Use `carelink_carepartner_api_login.py` from
   [carelink-python-client](https://github.com/ondrej1024/carelink-python-client),
   log in with the dedicated account, and you get `logindata.json`.

5. **Store the tokens, then delete the file**
   ```bash
   npx wrangler kv key put tokens --path logindata.json --binding CARELINK --remote
   rm logindata.json
   ```

6. **Deploy and test**
   ```bash
   npm test
   npx wrangler deploy
   curl -X POST -H "Authorization: Bearer <CLOCK_TOKEN>" https://sugarclock-bridge.emve.workers.dev/carelink/poll
   ```
   Check these in the output:
   - `carelink.error`: should be `null`.
   - `carelink.snapshot.last`, `alerts` and `banners`: real values.
   - `carelink.nightscout_upload`: `ok: true`, with entries and devicestatus
     uploaded.
   - `clock.source`: `carelink`.

   Then check ns10be: new readings, and the IOB pill matches the pump.

If the username lookup fails, set `CARELINK_USERNAME` (the care-partner login)
and `CARELINK_PATIENT_USERNAME` (Emilie's CareLink username) in `wrangler.toml`.
If the timestamps are an hour off, compare `carelink.snapshot.time_offset_h` in
`/debug`.

Switches:
- `NS_UPLOAD = "false"` stops the upload; the clock keeps working.
- `CARELINK_ENABLED = "false"` switches to Nightscout only (only useful with
  ns10be's poller back on).

## Setup: Worker basics

```bash
npm install
npx wrangler login
npx wrangler secret put NS_TOKEN      # Nightscout read token (follower-…)
npx wrangler secret put CLOCK_TOKEN   # e.g. output of: openssl rand -hex 24
npm test
npx wrangler deploy
```

## Routes

- `GET /clock`: the JSON above (Bearer `CLOCK_TOKEN`)
- `GET /debug`: everything behind it, including the raw CareLink snapshot summary, backoff status and poll decision
- `POST /carelink/poll`: force a CareLink fetch, returns the debug output
- `POST /reminders/clear?id=set` (`sensor`, `battery`, `reservoir`, `bg`, `all`): mark a remembered pump reminder as done by hand. The clock's "Clear pump alerts" button calls it with `id=all`
- `GET /health`: `{"ok":true}`, no auth

## Clock settings

- Data source: Custom URL, `https://sugarclock-bridge.emve.workers.dev/clock`
- Token field: the `CLOCK_TOKEN` value (sent as `Authorization: Bearer …`)
- TIR screen, pet + TIR and adaptive polling need the patched firmware
  (`sugarclock-full-firmware.patch`). Alerts, temp target and IOB on the display
  come with the next firmware round.
