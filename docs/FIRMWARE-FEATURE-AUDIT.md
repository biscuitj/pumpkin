# Pumpkin firmware: feature audit for the 1.0 rewrite

State audited: `firmware/` at 0.10.0 + Unreleased (USB guard, image marker), 13 Oct 2026.
Goal: decide per feature what a rewritten firmware keeps, reworks or drops.

> **Done in 1.0.0 (18 Oct 2026).** This is the planning record; §11 lists what
> the rewrite actually did with each decision. For the clock as it is now, see
> [`CLOCK-DISPLAY-SPEC.md`](CLOCK-DISPLAY-SPEC.md) and
> [`../firmware/README.md`](../firmware/README.md).

**Verdicts:** **KEEP** (port as is, behaviour proven) · **REWORK** (keep the idea, change the design) · **DROP** · **DECIDE** (product call, no clear technical answer).

## 0. The short version

- **Core to keep:** bridge data (`/clock`), glucose screen with stale handling and sport bars, TIR, IOB, pump alerts screen, low-glucose lock, time, Patch, night mode + Full Mute, Wi-Fi setup (portal, enterprise, net check), `.local` name, web OTA with image check + rollback, live display mirror, password.
- **Drop:** Pomodoro, stopwatch, countdown, retired screen placeholders, Dexcom Share, dead endpoints (`/api/history`, `/api/timer`), the LittleFS `config.json` overlay, numeric `force_mode`.
- **Decide:** LibreLinkUp and Nightscout-direct (only matter if other families/CGMs are a target), the 7 extra pets, clock-to-clock copy, buzzer alerts, Improv serial, CareLink on the clock.
- **Why rewrite at all:** the fetch blocks the render loop, the bridge token goes over unvalidated TLS, two big monoliths (`glucose_engine.cpp`, `web_server.cpp`), ~80-field config with dead fields, enum values frozen for API compat, flash 80 % full. Details in §9.

## Size snapshot

| | |
|---|---|
| Hand-written C++ (src + include) | ~11,900 lines (+ 7,000 generated `web_assets.cpp`) |
| Biggest files | `web_server.cpp` 1,450 · `glucose_engine.cpp` 1,252 · `http_client.cpp` 740 · `wifi_manager.cpp` 717 |
| Web UI source | `index.html` 107 KB · `style.css` 38 KB · 3 PNG icons ~64 KB · 5 pages |
| `firmware.bin` | 1.46 MB in a 1.79 MB OTA slot (≈ 80 %) |
| Hardware | ESP32-WROOM-32D, 4 MB flash, **no PSRAM** (~520 KB SRAM) |
| Stack | Arduino-ESP32 (platform 7.0.1), FastLED I2S, NeoMatrix/GFX, ArduinoJson 7, ESPAsyncWebServer |
| Tests | 25 host test files (C++ logic + pytest, incl. JS/C++ pet parity) |

---

## 1. Data sources (`http_client.cpp`, `libre_*.cpp`)

| Feature | What it does | Verdict | Notes |
|---|---|---|---|
| Custom URL (`data_source 0`) → Pumpkin bridge `/clock` | GET + Bearer token, parses glucose, trend, timestamp, `tir*`, `iob`, `temp_target`, `warnings[]`, `message`, `force_mode`, `next_poll_sec` | **REWORK** | Core. Make the `/clock` contract the *only* first-class source, versioned. Validate TLS (now `setInsecure()`, token is exposed to a MITM). |
| Nightscout `entries` array parsing | Same URL field, detects `sgv`/`date`/`direction` | **DECIDE** | ~40 lines. Only useful without a bridge. Keep if "any Nightscout family" is a target, else drop; the bridge already falls back to Nightscout server-side. |
| Adaptive polling (`next_poll_sec`) | Poll when the bridge says the next reading is due; normal interval after an error | **KEEP** | |
| Dexcom Share (`data_source 1`) | Two-step login, session per hour, US/OUS | **DROP** | ~200 lines, insecure TLS, not the 780G use case. |
| LibreLinkUp (`data_source 3`) | Login, region redirect, pinned roots, multi-patient picker, backoff | **DECIDE** (lean drop) | ~1,200 lines incl. root certs + config locking. The best-built source in the codebase, but irrelevant for Medtronic. Keep only if Pumpkin goes multi-CGM. |
| Demo mode (`data_source 2`) | Synthetic 80–100 mg/dL wander | **REWORK** | Keep as a dev/preview tool; extend to simulate lows/highs, IOB, TIR and pump alerts so every screen is demo-able without a pump. |
| Delta tracking | Delta vs previous distinct reading (dedup by CGM timestamp) | **KEEP** | Better: take delta from the bridge (it has the 24 h series). |
| 48-entry history ring + `GET /api/history` | Stores recent values | **DROP** | No page uses it. Re-add only if a sparkline screen is wanted. |
| `message` (server text) | Bridge can push a scrolling text | **REWORK** | Good channel; give it a priority/kind instead of a free string. |
| `force_mode` (server forces a screen by enum number) | | **DROP** → replace | Numeric enum values are why retired states can't be removed. Use named screens if needed at all. |
| `bridge_alert` (`CARELINK LOGIN NEEDED` / `ERROR`) | Sent by the bridge | **gap** | **Firmware ignores it.** An expired CareLink login only shows as stale data. Must be shown in the rewrite. |
| Bridge fields sent but unused | `sensor_hours_left`, `reservoir_units`, `pump_battery`, `phone_battery`, `temp_target_min`, `alerts[].text/since`, `trend_arrows`, `source` | **DECIDE** | Free data for new screens (e.g. "sensor 6 h", sport mode minutes left). |
| Clear reminders (`/reminders/clear?id=all`) | Clock derives the URL from `/clock` URL, POSTs from main loop | **KEEP** | Rework URL derivation into the contract (bridge advertises it). |

## 2. Screens (`glucose_engine.cpp` render switch)

| Screen | Current behaviour | Verdict | Notes |
|---|---|---|---|
| **Glucose** | Value in range colour, trend arrow, mg/dL or mmol/L | **KEEP** | |
| └ stale warning (10 min) | Dimmed to 1/3 + yellow `!` | **KEEP** | 10 min is hardcoded, stale timeout is configurable: unify. |
| └ stale (configurable, default 20 min) / 5 failures | Grey value, min brightness 20 | **KEEP** | |
| └ sport mode bars | 2 px bars cols 0–1, 30–31 while temp target | **KEEP** | |
| └ delta flash | On a new value, show delta 3 s (`show_delta`) | **DECIDE** | Nice, low cost; overlaps the Trend screen. |
| **Trend** | Arrow + delta | **DECIDE** | Redundant with the glucose arrow + delta flash. Pick one. |
| **Time in range** | Right-aligned %, green ≥70 / amber ≥50 / red | **KEEP** | |
| **IOB** | `0.35U` cyan, `--U` when unknown/stale | **KEEP** | |
| **Pump alerts** | Icons (pause, triangle, battery, reservoir, drop, `SENSOR`, `SET`), 5 s each, plays once per rotation, only in rotation while active | **KEEP** | The most valuable Pumpkin addition. Port the type-tracking logic + tests. |
| **Time** / **Date** | Blinking colon, 12/24 h, date alternates every 5 s, 3 date formats | **KEEP** | Trim date formats to 1–2. |
| **Pet** (companion) | 8 pets, 3 styles (text/icon/only), status from glucose range, sleepy in rest hours, waves on right button, seasonal surprises (Halloween, New Year) | **KEEP** Patch · **DECIDE** the rest | Pixel data is small; the cost is UI + JS parity. Keep the shared renderer + parity test idea. |
| Pomodoro timer | Work/break/long-break, buzzer | **DROP** | SugarClock leftover; unrelated. |
| Stopwatch | | **DROP** | |
| Countdown to event | Name + target date | **DROP** | Or repurpose as "sensor change in X" from bridge data. |
| Message | Static ≤5 chars, else scroll | **REWORK** | See `message` above. |
| Boot | Pixel pumpkin, then "Pumpkin" scroll 3 s | **KEEP** | |
| Status: NO WIFI / NO DATA / NO CFG / SETUP AP / NET LIMITED / connection info | Scroll texts with address, AP name, trial result, blocked host | **KEEP** | Consolidate into one status-screen module; texts are good. |
| Weather / sysmon / notify placeholders | Retired enum values kept for `force_mode` | **DROP** | |

**Engine behaviour around the screens**

| Feature | Verdict | Notes |
|---|---|---|
| Rotation built from toggles (`engine_rebuild_toggle_order`, every 5 s) | **REWORK** | Becomes a screen registry: each screen declares `enabled()`, `available()`, `duration()`, `render()`. |
| Auto-cycle (3–300 s) | **KEEP** | |
| Alerts screen sets its own duration; "alerts replace pet" option | **KEEP** | Falls out of the registry naturally. |
| Fade out / fade in transitions (600 ms, ~20 fps) | **KEEP** | |
| **Low-glucose lock** (`glucose_only_when_low`): fresh low pins the glucose screen, cancels fades, blocks buttons/previews/connection info | **KEEP** | Safety feature. Consider making it default on. |
| Urgent glucose beats connection info overlay | **KEEP** | |
| Server/web force override (`engine_force_state`) | **DROP** | Only reached via left/right long press "clear overrides". |

## 3. Alerting

| Feature | Verdict | Notes |
|---|---|---|
| Buzzer glucose alerts (`alert_enabled`, low/high, beep every 10 s, snooze 15 min via middle long press) | **DECIDE** | **No UI**: only reachable through `POST /api/config`. Off by default. In a classroom a beeping clock may be unwanted; at home it might be the point. If kept: UI, distinct patterns, and also for pump alerts. |
| Night mode (start/end hour, night brightness) | **REWORK** | Whole hours only, every day. A school clock wants weekday schedules and minutes (e.g. Mon–Fri 08:15–15:45 on). |
| Full Mute (black + silent, nothing wakes it, fetching continues, off without valid time) | **KEEP** | Rules are right; fold into the schedule rework. |
| Pump alert previews from the web UI (never over an urgent low) | **KEEP** | |

## 4. Display & hardware

| Feature | Verdict | Notes |
|---|---|---|
| 32×8 WS2812B via FastLED I2S + NeoMatrix/GFX, 5×7 font | **KEEP** | Re-evaluate whether GFX/NeoMatrix are needed vs. a tiny own framebuffer + font (flash). |
| Brightness: manual, auto (LDR, 10-sample average), middle button cycles 10/40/100/200 | **KEEP** | Middle press silently turns auto off: surprising, rework. |
| Range colours (6 configurable) + clock colour | **KEEP** | Could shrink to presets. |
| mg/dL ↔ mmol/L | **KEEP** | Needed for any family outside BE/US-style mg/dL. |
| Thresholds (urgent low/low/high/urgent high) | **KEEP** | Separate from buzzer thresholds today: merge. |
| Battery voltage/percent (ADC 34) | **DECIDE** | Shown in debug only; TC001's battery is small. |
| DS1307 RTC (I2C 21/22) + NTP + POSIX TZ | **KEEP** | `time_is_network_synced()` vs RTC distinction matters for data age: keep it. |
| Buzzer (LEDC, non-blocking queue) | **KEEP** (driver) | |
| Live LED mirror (`/api/display/frame`, viewing lease, ETag) | **KEEP** | Great for support and screenshots. |

## 5. Buttons

| Gesture | Today |
|---|---|
| Left short | next screen |
| Left long | clear overrides, back to glucose |
| Middle short | brightness cycle (turns auto off) |
| Middle double | show `.local` address + IP |
| Middle long | snooze buzzer |
| Right short | context: timer start/pause · stopwatch · pet wave · else previous screen |
| Right long | context: timer/stopwatch reset · else clear overrides |

**REWORK.** Seven gestures, two of them for features being dropped. For a 6-year-old and a teacher: left/right = prev/next, middle short = wave/acknowledge, middle long = address. Brightness belongs in the web UI or a hidden combo. Consider a button lock for the classroom.

## 6. Connectivity

| Feature | Lines | Verdict | Notes |
|---|---|---|---|
| Wi-Fi state machine (connect, retry, setup AP after continuous failure) | 717 | **KEEP** | |
| Setup portal: open AP `Pumpkin-Setup-XXXX`, captive DNS + OS probe pages, scan, trial connect (nothing saved until it works), status strings | 117 + part of web | **KEEP** | Solid UX; port the trial flow. |
| WPA2-Enterprise (PEAP / EAP-TTLS, anon identity, optional CA upload to LittleFS) | in wifi/config | **KEEP** if the school needs it, else **DECIDE** | School networks are why it exists. |
| Net check (DNS / data host / NTP probes, `NET LIMITED` screen with the blocked host) | 229 | **KEEP** | Tells a school IT person what to allow. |
| Improv serial (Wi-Fi via ESP Web Tools over USB) | 313 | **DECIDE** | Only pays off with a browser flasher/installer page. Fits the "smooth setup for other families" idea. |
| Clock identity: name, slug with accent folding, `name.local`, `-2` on conflicts, DHCP hostname | 210 | **KEEP** | |
| Peer discovery (`_pumpkin._tcp`, list, scan) + copy settings to other clocks | ~400 | **DECIDE** | Useful for home + school clocks; but it's also the password-forwarding path. Cheaper alternative: export/import a settings JSON. |

## 7. Web UI & API

| Feature | Verdict | Notes |
|---|---|---|
| Embedded assets (generated `web_assets.cpp`, gzip when smaller, ETag) | **KEEP** | Approach is right; no LittleFS image needed. |
| Pages: Dashboard (mirror, latest BG, state), Configuration (7 tabs), Debug, Device (network, memory, update, buttons, power), debug_canvas | **REWORK** | Rebuild as one small app (Preact/Vue + Vite → single gzipped bundle). Merge Debug + Device. Drop the 64 KB of PNG icons (inline SVG). |
| Config tabs: Blood Sugar, Display, Display Pet, Time & timers, Wi-Fi, Clocks, Reset | **REWORK** | Shrinks a lot once timers, Dexcom/Libre go. |
| Remote control prev/next | **KEEP** | |
| Optional password (HTTP Basic, user `pumpkin`, probes exempt) | **KEEP** | Plain HTTP on the LAN; acceptable, document it. |
| `POST /api/test/glucose` (test fetch) | **REWORK** | Runs a blocking HTTPS fetch on the async web task, concurrently with the main loop's fetch → shared-state race. Should enqueue on the network task. |
| Restart / factory reset | **KEEP** | |
| LittleFS `/config.json` overlay applied at boot | **DROP** | Installer leftover from SugarClock. |
| `GET /api/timer`, `GET /api/history` | **DROP** | No consumer. |

## 8. Updates, safety & tooling

| Feature | Verdict | Notes |
|---|---|---|
| Web OTA upload to inactive slot, pauses HTTP, progress screen | **KEEP** | |
| Image marker check (`PUMPKIN-IMAGE|ulanzi-tc001-esp32-4mb|`), browser pre-check, build fails without marker | **KEEP** | |
| First-boot validation (config loaded, assets present, loop count, heap) + automatic rollback + "rolled back" report | **KEEP** | |
| USB upload guard (`upload_guard.py`) | **KEEP** | |
| Task watchdog 30 s | **KEEP** | Timeouts were tuned around blocking fetches; less fragile once networking is off the loop. |
| `-fstack-usage` + layout check script | **KEEP** | |
| Host tests incl. JS ↔ C++ pixel parity | **KEEP** | Design the rewrite so render code is pure and host-testable from the start. |
| `libre_probe.py` | **DROP** with Libre | |

## 9. Architectural problems the rewrite should fix

1. **Network I/O on the render loop.** `http_loop()` runs inside `loop()`; a fetch blocks rendering for seconds (handshake 8 s + connect 5 s + read 10 s). The display freezes mid-fade or mid-alert, and button presses are dropped. → Network task (fetch, clear-reminders, net check) + render task, exchanging an immutable snapshot struct.
2. **TLS without validation** for the bridge (Bearer token) and Dexcom. → Cert bundle (`esp_crt_bundle`) or pinned roots for `workers.dev`/Let's Encrypt; only Libre does this right today.
3. **Concurrency by accident.** Web handlers call into engine/http state from the AsyncTCP task (test fetch, previews use ad-hoc volatile flags). → One command queue into the owning task.
4. **Monoliths.** One `switch` renders 20+ states with duplicated stale checks (`age >= stale_ms || failures >= 5` appears 7×). → Screen registry + one `DataFreshness` function.
5. **Config.** ~80 fields in one struct, NVS keys spread across `config_manager.cpp`, many for dropped features. → Versioned schema, one table of key/default/range used by NVS, JSON API and UI.
6. **Frozen enums.** Screen numbers can't change because `force_mode` exposes them. → Named screens, no numeric API.
7. **Flash at 80 %.** Dropping Libre, Dexcom, timers, PNGs and the UI rewrite should free several hundred KB; needed if anything heavy (CareLink client) moves onto the clock.

## 10. CareLink on the clock? (DECIDE, separate from the rewrite)

Feasible, but heavy on a no-PSRAM ESP32:
- OAuth refresh with a rotating refresh token → must persist to NVS on every refresh (flash wear ~24×/day: fine), and a failed write loses the login.
- `display/message` returns 24 h of `sgs` + `markers` + notifications: needs ArduinoJson streaming filters to stay within heap next to TLS (~40–50 KB per session).
- The bridge also does TIR, remembered reminders (latches, tubing-fill detection, 72 h expiry), alert classification (xDrip+ table, GPL-3.0) and the Nightscout upload. All would need porting, and the GPL text table would make the firmware GPL.
- Two clocks = two CareLink clients on one care-partner account, which is what broke ns10be before.

Suggestion: keep the Worker for the rewrite's v1 but put the data source behind one interface (`Snapshot fetch()`), so an on-device CareLink source can be added later without touching screens.

## Proposed v1 scope of the rewrite

- Data: bridge `/clock` (versioned, validated TLS) + demo source. Show `bridge_alert`.
- Screens: glucose (stale, sport bars), TIR, IOB, pump alerts, time/date, Patch, status screens.
- Engine: registry, auto-cycle, fades, low-glucose lock, night schedule with Full Mute.
- Platform: Wi-Fi + portal + enterprise + net check, `.local` name, password, web OTA + marker + rollback, live mirror.
- Later / optional: extra pets, buzzer alerts, peer copy, Improv/web flasher, Nightscout-direct, Libre, on-device CareLink.

## 11. Outcome: what 1.0.0 did

| Decision | 1.0.0 |
|---|---|
| Data sources | Bridge `/clock` + demo only. Dexcom, LibreLinkUp and Nightscout-direct dropped; the bridge still falls back to Nightscout. |
| TLS to the bridge | Certificate checked against pinned roots (Let's Encrypt, Google Trust Services, SSL.com, DigiCert, USERTrust, Amazon). |
| `bridge_alert` (gap) | Shown: scrolls in amber right after the glucose. |
| `message` | Scrolls in the rotation. No priority/kind yet. |
| `force_mode` | Ignored by the clock (the bridge still sends `-1` for stock SugarClock). |
| Demo source | Reworked: a 3-minute glucose loop through every range, every pump alert, TIR and IOB, a new reading every 5 s. |
| Delta flash vs Trend screen | Delta flash kept (setting, on by default); Trend screen dropped. |
| Stale warning | Unified: the warning starts at half the stale time. |
| Pets | All eight kept, Patch the default; the dashboard previews them from the clock's own pet code. |
| Timers, stopwatch, countdown, placeholders | Dropped. |
| Buzzer alerts | Dropped; the buzzer pin is held low. |
| Night mode | Reworked into a schedule: awake hours per weekday in minutes, dim or dark (Full Mute) at night. |
| Brightness | Auto (light sensor) or manual, from the dashboard; no button for it. |
| Colours | Three colour sets (classic, colour-blind friendly, calm) instead of free colours; gamma correction kept. |
| Buttons | Left/right previous/next, middle wave, middle hold address. |
| Wi-Fi, setup hotspot, enterprise, net check | Ported; checks run on the network task. NTP blocked → time from the bridge's `Date` header. |
| Peer discovery + copy | Dropped. mDNS name kept. |
| Improv serial | Dropped. |
| Web UI | New Preact dashboard (Display, Pet, Schedule, Connections, Device) plus a setup page, single files gzipped into the firmware. |
| API | One contract (`dashboard/src/contract.ts`); source test and alert clearing queued on the network task. |
| Config | Settings per group as JSON in NVS (`pumpkin`), validated from one table; 0.x settings carried over once. |
| Architecture (§9) | Pure core (`src/core`, host-tested, simulator) + ESP32 layer (`src/hal`); network task, render loop and web server share one locked state. No GFX/NeoMatrix. |
| Updates | Upload check, first-boot validation and rollback kept; USB guard recognises 1.0 clocks. |
| CareLink on the clock (§10) | Not done; the bridge stays. |
| Size | 1.39 MB, 75 % of an OTA slot (was 80 %). |
