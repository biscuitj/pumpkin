# Changelog

All notable changes to Pumpkin: the bridge (Cloudflare Worker) and the clock
firmware (started from SugarClock 0.2.15, rewritten in 1.0.0).

The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and
the project uses [Semantic Versioning](https://semver.org/).

How versions work:
- **One version for the whole project.** The bridge and the firmware share the
  `/clock` contract, so they are versioned together.
- **Bridge:** the version is in `bridge/package.json`.
- **Firmware:** the version is in `firmware/VERSION` (from 0.8.0 on; before
  that it stayed at SugarClock's `0.2.15` and the project version was only
  recorded here).

## [Unreleased]

### Added
- **Two sensor alerts.** Yellow `SENSOR` (`sensor`) in the hours before the
  sensor ends, red `! SENSOR !` (`sensor_change`) while the pump's
  replace-sensor alert is on or remembered. The bridge sends `sensor` next to
  `sensor_change`, so firmware before this still shows the yellow one; this
  firmware shows only the red one.

### Fixed
- Bridge: **sensor reminder stuck after a sensor change.** With an expired
  sensor there's no live sensor value, so the reminder took its baseline from
  the first reading back, the new sensor's (~167 h), and could only end after
  72 h (23 Oct). The sensor reminder now ends on a warm-up after the alert:
  only a new sensor warms up, and every SG in the CareLink data carries the
  sensor state, so a warm-up between polls is seen too.
- Bridge: a reminder that starts while the pump is out of range takes its
  baseline from the last live value before the alert (`snapshot.seen`), so a
  battery or reservoir swapped meanwhile still ends it.

## [1.0.0] - 2026-10-18

The clock firmware is rewritten around a new dashboard and reads only the
Pumpkin bridge. Clocks on 0.x keep their settings when they update (from the old
web page or over USB). The bridge gets an earlier sensor warning; its `/clock`
answer is unchanged.

### Added
- Firmware: **new dashboard** (`firmware/dashboard/`, Preact), served by the
  clock and built into the firmware: Display (with a live mirror of the
  matrix and previews of alerts), Pet, Schedule, Connections, Device. A
  separate setup page over the setup hotspot.
- Firmware: **Schedule:** awake hours per day of the week instead of one night
  window; at night the clock dims or goes dark.
- Firmware: **source test:** Connections tests the bridge address and token on
  the clock and says which part fails (address, token, CareLink).
- Firmware: **time without NTP:** when the network blocks NTP, the clock sets
  its time from the bridge's HTTPS answers.
- Firmware: **Pet tab** with previews: the picker shows each pet as the clock
  draws it (`GET /api/display/pets`, from the clock's own pet code); the
  chosen one animates.
- Firmware: **updates from the dashboard** (Device → Firmware): the file is
  checked in the browser and on the clock while it streams in, and a new
  version that doesn't start properly is rolled back by itself, as before.
- Firmware: **host simulator** (`firmware/host/`): the real API, engine and
  rendering on a computer, for working on the dashboard without a clock.
- Bridge: **sensor warning before the sensor ends.** The pump only alerts when
  the sensor has expired, and a new sensor needs about 2 h to warm up. The
  `sensor` warning now also comes on when the sensor has `SENSOR_WARN_MIN`
  minutes or less left (default 180, `0` turns it off), and goes away with
  the new sensor. Not during a warm-up, and not without a live sensor value.
  On 16 Oct: 44 min left and no warning on the clock.

### Changed
- Firmware: **rewritten** as a core of plain C++ (`src/core`, unit-tested on
  the host) and a thin ESP32 layer (`src/hal`). One HTTP API, described in
  `dashboard/src/contract.ts`. Settings are stored per group as JSON in NVS
  (namespace `pumpkin`); a clock coming from 0.x carries its settings over once
  (Wi-Fi, bridge, name, password, most display settings, night hours) and then
  clears the old ones. The time zone isn't carried over: check it on Device.
- Firmware: **HTTPS to the bridge checks the certificate** against pinned root
  certificates (Let's Encrypt, Google Trust Services, SSL.com and a few more)
  instead of trusting any server.
- Firmware: buttons: left and right step through the screens, middle makes the
  pet wave, holding middle shows the clock's address. Brightness and screen
  modes are set from the dashboard.
- Firmware: three colour sets (classic, colour-blind friendly, calm) instead of
  free colours. LED colours keep the old firmware's gamma correction.

### Removed
- Firmware: Dexcom Share, LibreLinkUp and direct Nightscout; the clock reads the
  Pumpkin bridge only (or demo data). The bridge still falls back to Nightscout.
- Firmware: the trend screen, timers, stopwatch, countdown, buzzer glucose
  alerts, copying settings between clocks, Improv serial setup and the
  `force_mode` field.
- Firmware: an urgent high or low no longer replaces the pet screen with the
  number. Pump alerts can still take the pet's place (Display → Pump alerts).
- Firmware: the old web pages (`data/www`).

## [0.12.0] - 2026-10-14

The set change reminder also clears when the pump was suspended, filled and
resumed between two polls. The firmware is unchanged apart from the version.

### Changed
- Bridge: **set change detection without a seen suspend.** Until now a tubing
  fill only counted when a poll caught the pump suspended. With polls about
  5 minutes apart, a quick set change could fall between two polls and the
  `set` reminder stayed on. Now a reservoir drop of ≥ 5 U that no recorded
  bolus or basal explains also counts without a suspend, if the two pump
  readings are at most 12 minutes apart, both have active insulin, and IOB rose
  by less than 0.3 U. A fill adds nothing to IOB; a bolus whose record hasn't
  reached CareLink yet does. Over a longer gap a bolus may have worn off in
  IOB, so then a suspend is still required. On 14 Oct: −9.6 U in 5 min, IOB
  1.40 → 1.25.

## [0.11.0] - 2026-10-13

Seven new display pets, and firmware only goes to the device you meant: a USB
guard for developers and a check on every uploaded update. The bridge is
unchanged apart from the version.

### Added
- Firmware: **seven new display pets** (ids `8`–`14`): Toffee the cat, Kiko the
  frog, Domino the penguin, Hoot the owl, Leo the lion, Stella the unicorn
  (rainbow mane) and Kobi the crab. Each has its own in-range, low, high,
  resting and hello poses, same as the others; resting pets show a small
  drifting "z". Same pixels in the web preview (parity-tested). Clocks keep
  the pet they have.
- Firmware: **USB guard** (`scripts/upload_guard.py`) before `pio run -t upload`,
  `uploadfs` and `erase`. It picks the single CH340 device that is connected
  (or the `--upload-port` you give), reads its chip, MAC, flash size and clock
  settings (read only), and fixes PlatformIO to that port. A clock that already
  runs Pumpkin or SugarClock is updated without a question; any other device,
  and every erase, needs a `y`. No clock or more than one: it stops. Without a
  terminal it refuses unless `PUMPKIN_DEVICE` lists the device's MAC;
  `PUMPKIN_SKIP_USB_GUARD=1` turns it off.
- Firmware: **update check**. The clock only installs a `.bin` that is an ESP32
  app image carrying this build's marker `PUMPKIN-IMAGE|ulanzi-tc001-esp32-4mb|`.
  Other projects, other chips and Pumpkin builds for other hardware are refused
  and the clock keeps running what it has. The Device page checks the file in
  the browser before uploading, and the build fails if `firmware.bin` lacks the
  marker.
- Firmware: the Device page shows **which clock** (name and `.local` address)
  next to the file picker and in the install confirmation.
  `GET /api/update/status` adds `clock_name`, `clock_host`, `clock_id`,
  `hardware` and `image_id`.

### Changed
- Firmware: `PUMPKIN_HARDWARE_ID` is defined once, in
  `include/ota_image_check.h` (no longer injected by `pre_build.py`).

### Notes
- Firmware older than this version has no marker, so a clock running this
  version won't take an older one through the web page. Go back over USB
  instead.

## [0.10.0] - 2026-10-12

A pumpkin joins the display pets, and the clock drops the features a glucose
clock doesn't need. The bridge is unchanged apart from the version.

### Added
- Firmware: **Patch the pumpkin**, a new display pet (id `7`). Smiles and
  flutters its leaf in range, squashes down with a frown when low, flaps its
  vine arms with an open mouth when high, closes its eyes in rest hours and
  waves a vine to say hello. Same pixels in the web preview (parity-tested).
  New clocks start with Patch; clocks that already have a pet keep it.

### Removed
- Firmware: **weather** (OpenWeatherMap screen, settings, "Test Weather API",
  weather previews, the goldfish's weather effects). The clock no longer
  contacts `api.openweathermap.org`.
- Firmware: **Integrations**: push notifications (`POST /api/notify`,
  `/notify.html`) and the system monitor screen (`POST /api/sysmon`).
- Their settings are deleted from the clock's flash on the next save, including
  a stored weather API key. A default screen of "weather" becomes blood sugar.
  The screen numbers stay reserved, so `force_mode` values keep their meaning;
  forcing one of the removed screens is ignored. Old `#weather` and
  `#integrations` links open the Blood Sugar tab.

## [0.9.1] - 2026-10-12

### Fixed
- Bridge: the **BG required** reminder stayed on the clock after the pump went
  back to SmartGuard by itself (Safe Basal → Auto Basal without a finger BG).
  SmartGuard's BG required alerts (819, 821, 822, 829) now also clear when the
  pump is in Auto Basal again, which is when the pump's blue bar goes away. The
  BG check reminder (103) still waits for a finger BG. A reminder that is stuck
  now clears on the first poll after deploying.

## [0.9.0] - 2026-10-12

Pumpkin can be installed by other families: a new install guide without
anything specific to one setup, a CareLink login that is one command, and a
bridge that no longer needs Nightscout. The firmware itself is unchanged apart
from the version; a clock on 0.8 doesn't need the update.

### Added
- Bridge: **`npm run login`**, a CareLink login in Node that replaces
  carelink-python-client. It opens the installed Google Chrome (no browser
  download, via `playwright-core`), catches the login, exchanges the code for
  tokens and stores them in KV with Wrangler. It also resets the account config
  and the error backoff, so the new login is used right away. Options:
  `--country`, `--us`, `--out tokens.json`.
- Bridge: **`LANG`** (`en` or `nl`) for the short alert texts in `/clock`
  (`alert`, `alerts[].short`), the Nightscout device status and `bridge_alert`.
- Bridge: `LICENSE` (GPL-3.0, because of the xDrip+ alert texts).
- Docs: **`docs/INSTALL.md` rewritten for a new family**: accounts, deploying
  the bridge, the login, flashing and setting up the clock, Nightscout as an
  option, troubleshooting, and notes for existing installs.

### Changed
- Bridge: **runs without Nightscout.** Leave `NS_URL` empty and the bridge works
  on CareLink alone; nothing calls Nightscout.
- Bridge: the default language is **English**. `bridge_alert` is now
  `CARELINK LOGIN NEEDED` / `CARELINK ERROR` (Dutch `… NODIG` / `… STORING` with
  `LANG = "nl"`). Existing installs: add `LANG = "nl"` to keep the Dutch texts.
- Bridge: `wrangler.toml.example` is generic: Worker name `pumpkin-bridge`,
  Nightscout settings marked optional, `NS_UPLOAD = "false"` until you turn it on.
  The code default for an existing `wrangler.toml` without `NS_UPLOAD` stays
  "on", so running installs keep uploading.
- Bridge: package renamed `pumpkin-bridge`, version 0.9.0, Node 20 or newer.
  Deployed Workers keep their name; Nightscout data is still posted as device
  `sugarclock-bridge`, so existing data stays recognised.
- Bridge README, data schema and display spec: Pumpkin names, no personal
  setup details.
- Firmware: version 0.9.0. `firmware/INSTALL.md`: the LittleFS image is
  optional (the web UI is built into the firmware), the school allowlist names
  the bridge instead of GitHub, and the address example says `pumpkin-…`.

## [0.8.0] - 2026-10-12

The firmware becomes **Pumpkin**: it branches off SugarClock 0.2.15 for good,
with its own name, version and update route. Clocks on 0.7 or older need one USB
flash to get here; after that, updates go through the web page.

### Added
- Firmware: **update firmware from the web page** (Device → Update firmware,
  `POST /api/update?size=…`, status via `GET /api/update/status`). The image
  goes into the backup slot and is accepted after the existing first-boot
  checks (≈ 15 s). If it doesn't come up healthy, the clock returns to the
  previous version by itself, and the Device page says so.
- Firmware: **optional password** for the web page and the whole API (HTTP
  Basic, user `pumpkin`, NVS `web_pw`), on the Clocks tab. Body and upload
  handlers check it before acting on any data. The captive-portal probe pages
  stay open. Copying settings to other clocks sends this clock's password.
  Scripts use `curl -u pumpkin:<password>`. A forgotten password is only
  cleared by a USB erase.
- Firmware: `LICENSE` (MIT, SugarClock copyright kept) and a firmware README.

### Changed
- Firmware: **renamed to Pumpkin.**
  - Firmware version `0.8.0` (`firmware/VERSION`, injected as `PUMPKIN_VERSION`).
  - Default clock name `pumpkin-xxxx` and setup network `Pumpkin-Setup-XXXX`.
    Clocks you already named keep their name.
  - Clocks announce themselves as `_pumpkin._tcp`, so clocks on 0.7 and 0.8
    don't see each other until both are on 0.8.
  - Page titles, logo (a pixel pumpkin), boot screen and messages now say
    Pumpkin; the sidebar credits SugarClock.
- Firmware: the setup network page, the install guide and the update docs
  describe the Pumpkin flow.

### Removed
- Firmware: **SugarClock's update server and check-in.** The clock no longer
  contacts `fleet.sugarclock.com`, never installs SugarClock releases, and has
  no automatic or scheduled updates (the "auto update" settings are gone).
  Removed with it: the fleet service, signed manifests and release key, the
  Mac installer app, the SugarClock website and their tests.

## [0.7.0] - 2026-10-12

Multiple clocks on one network. Firmware only; the Worker is unchanged.

### Added
- Firmware: **clock name** setting (`clock_name`, NVS `clk_name`) on a new
  **Clocks** tab. The default name is `sugarclock-xxxx`, from the last four
  characters of the chip ID. The name shows in the web page title and sidebar.
- Firmware: **`.local` address** via mDNS, derived from the name: "Slaapkamer
  Élise" becomes `slaapkamer-elise.local`. Accents are folded and other
  characters become dashes. A name another device already answers for gets
  `-2`, `-3`, … A rename takes effect without a restart. The router's client list
  (DHCP hostname) shows the new name after the next restart.
- Firmware: each clock announces itself as `_sugarclock._tcp` (TXT `name`, `ver`,
  `id`) and finds the others about once a minute. `GET /api/clocks` lists this
  clock and the others; `POST /api/clocks/scan` searches straight away. The
  Clocks tab lists them with links to their own pages.
- Firmware: **copy settings to other clocks** (`POST /api/clocks/copy`, status
  via `GET /api/clocks/copy`). The clock itself sends its saved screens,
  colours, glucose ranges, buzzer alerts and pet settings to the ticked clocks.
  Night mode / Full Mute hours and the data source (URL + token) only go when
  ticked. Wi-Fi, name, brightness and weather always stay per clock. It only
  sends to clocks it found itself.
- Firmware: after joining Wi-Fi from the setup page, the clock scrolls its new
  address once ("To connect, visit keuken.local or 192.168.0.12"). The connection
  info (middle button, double press) and the "visit … to setup" text show the
  `.local` address too.

### Changed
- Firmware: the setup Wi-Fi network is unique per clock,
  `SugarClock-Setup-XXXX` (same code as the default name), so two new clocks set
  up side by side are never mixed up. The USB (Improv) fallback uses the same
  name.
- Firmware: all mDNS and clock-to-clock traffic runs on its own task
  (`clocknet`), so the display and web server never wait on the network.

## [0.6.0] - 2026-10-12

### Added
- Worker: `suspended` warning for a pump stopped by hand (`pumpSuspended` or
  the banner `DELIVERY_SUSPEND`, without a SmartGuard auto-suspend). It comes
  first in the warning order and is sent without a delay.
- Worker: `/debug` shows the suspend state (`manual`, `auto` or `null`).
- Firmware: pause icon for `suspended`: two orange 2×6 bars, 2 px apart, 5 s.
- Firmware: **Full Mute** night mode option (`night_full_mute`, NVS
  `night_mute`). During night hours the screen is fully black and the buzzer is
  silent, and nothing wakes it (no urgent low, no stale-screen minimum
  brightness). Meant for a clock at school after school hours.
- Firmware web UI: **Clear pump alerts** button. `POST /api/alerts/clear` asks
  the HTTP loop to call the Worker's `/reminders/clear?id=all` with the clock
  token, and the glucose fetch runs on the next loop pass.
  `GET /api/alerts/clear` reports the result: `cleared`, `nothing` or `error`.
- Firmware web UI: Pause preview button. "All" now includes the pause.

### Changed
- Worker: SmartGuard auto-suspend (alerts 809/810, or a banner matching
  `SUSPEND…ON_LOW` / `SUSPEND…BEFORE_LOW`) maps to the general `warning`
  triangle.
- Firmware: `SENSOR` (narrow 4×7 letters) and `SET` (normal font) are shown
  static for 5 s instead of scrolling `… VERVANGEN`. Both are yellow. Every
  pump alert is now a 32×8 image shown for 5 s.
- Firmware: icon colours. The battery is light blue (#64B5F6) and the
  reservoir is green (#34A853). The blood drop stays red and the triangle stays
  amber.

### Fixed
- Firmware: the alerts screen tracks the alert on screen and the alerts already
  played by type instead of by position, so no alert is skipped when another
  one clears mid-sequence.

## [0.5.1] - 2026-10-12

### Changed
- Worker: `RAW_CAPTURE` is off again. The raw CareLink recording found what we
  needed (the tubing fill on a set change) and holds personal data.

## [0.5.0] - 2026-10-11

### Added
- Worker: **remembered reminders.** Set, sensor, battery, reservoir and BG
  reminders stay on the clock after they're dismissed on the pump, until the
  pump data shows the action was done:

  | reminder | clears when |
  |---|---|
  | set | a tubing fill is detected |
  | sensor | sensor time left goes up by ≥ 12 h |
  | battery | the battery % goes up |
  | reservoir | the reservoir goes up by ≥ 5 U |
  | BG | a finger-prick BG comes in |

  Reminders also clear after 72 h, or by hand.
- Worker: picks up alerts dismissed between two polls from CareLink's cleared
  notifications (last 2 h).
- Worker: **set change detection** from the tubing fill: while the pump is
  suspended, the reservoir drops ≥ 5 U more than the insulin that was delivered.
- Worker: `POST /reminders/clear?id=<id|all>` (Bearer auth) clears a reminder
  by hand.
- Worker: `/debug` shows cleared notifications, active reminders and the last
  detected set change.
- Worker: `RAW_CAPTURE` debug option that stores the raw CareLink data in
  Nightscout (device `sugarclock-bridge-raw`).

### Fixed
- Worker: a set change reminder disappeared from the clock as soon as it was
  dismissed on the pump, even though the set hadn't been changed yet.

## [0.4.0] - 2026-10-10

### Added
- Worker: `warnings` list in `/clock`, deduplicated and in display order:
  `warning`, `battery`, `reservoir`, `bg`, `sensor`, `set`. It's empty when the
  pump data is stale.
- Firmware: **pump alerts screen.** It joins the rotation only while there's
  something to show, after the pet screen. Each alert shows for 5 s or one
  scroll, then the rotation moves on.
  - Icons: battery, reservoir, blood drop (BG required), and a warning triangle
    for delivery, set connect, sensor signal and calibration.
  - Texts: `SENSOR VERVANGEN` and `SET VERVANGEN`.
- Firmware: options "Show Pump Alerts Screen" (`alrt_en`) and "Pump Alerts
  Replace the Pet Screen" (`alrt_pet`).
- Firmware: **sport mode.** While a temp target is active, the glucose screen
  gets a 2 px full-height bar on each side, in the value's colour.
- Firmware: **active insulin (IOB) screen**: `0.35U` in cyan, or `--U` when
  unknown. It has its own toggle (`iob_en`).
- Firmware web UI: alert preview buttons (`POST /api/test/alert`). They never
  override an urgent low.

## [0.3.1] - 2026-10-10

### Fixed
- Worker: token refresh got a 403 on the first poll. The Worker no longer
  refreshes a login that's still fresh, sends the app's headers, and reports
  the refresh error in detail.
- Worker: a 403 without an OAuth error body (a firewall) is treated as a normal
  error, not as a rejected login.
- Firmware: stale screens keep a minimum brightness of 20, so a dim clock with
  old data doesn't look switched off.
- Firmware: TLS handshake (8 s) and connect (5 s) timeouts, so a stalled
  connection can't trip the 30 s watchdog and reset the clock.

### Changed
- Install guide: the CareLink login uses Chrome instead of Firefox, makes a new
  clock token, and checks the Nightscout API password via `/api/v1/verifyauth`.

## [0.3.0] - 2026-10-09

### Added
- Worker: **the only CareLink client.** It logs in as a care partner (EU), with
  a rotating refresh token kept in KV, and serves the clock from CareLink data:
  - the pump's own trend and active insulin;
  - the temp target;
  - sensor time, reservoir, pump battery and phone battery;
  - active pump alerts as short Dutch texts, classified with xDrip+'s TextMap
    (GPL-3.0).
- Worker: request-driven polling around the expected next reading, a `*/30`
  cron that keeps the token alive, backoff on failures, and `bridge_alert` when
  the login needs redoing or CareLink keeps failing.
- Worker: **Nightscout upload** after each fetch, posting only what's missing:
  - `entries`, including gap filling up to 24 h;
  - `devicestatus`, so Nightscout shows the pump's own IOB, reservoir and
    battery;
  - `treatments`: boluses, carbs, finger BGs and temp targets.

  This replaces Nightscout's own CareLink poller.
- Worker: Nightscout stays the fallback for glucose and time in range.
- Docs: `INSTALL.md` (step-by-step setup) and `DATA-SCHEMA.md`.

## [0.2.0] - 2026-10-09

### Added
- Worker: **first bridge** between Nightscout and the clock. `/clock` serves
  SugarClock's custom-URL JSON plus extra fields:
  - a slope-based trend when the feed says `NOT COMPUTABLE`;
  - 24 h time in range;
  - optional IOB;
  - `next_poll_sec`.

  It fails with 502 or 503 rather than guess. `/debug` is included.
- Firmware: **time in range screen** (right-aligned %, green ≥ 70, amber ≥ 50,
  else red) with its own toggle (`tir_en`).
- Firmware: **adaptive polling.** The clock polls when the bridge says the next
  reading is due (`next_poll_sec`), and falls back to the normal interval after
  an error.
- Firmware: the pet companion can show time in range instead of LOW / OK / HIGH
  (`pal_tir`, on by default).

## [0.1.0] - 2026-10-08

### Added
- Firmware: Nightscout `/api/v1/entries` support. It parses the array format
  and dates the reading by its CGM timestamp, so staleness reflects the real
  age.
- Firmware: "Show Trend Screen" toggle (`trend_en`).

[Unreleased]: #unreleased
[1.0.0]: #100---2026-10-18
[0.12.0]: #0120---2026-10-14
[0.11.0]: #0110---2026-10-13
[0.10.0]: #0100---2026-10-12
[0.9.1]: #091---2026-10-12
[0.9.0]: #090---2026-10-12
[0.8.0]: #080---2026-10-12
[0.7.0]: #070---2026-10-12
[0.6.0]: #060---2026-10-12
[0.5.1]: #051---2026-10-12
[0.5.0]: #050---2026-10-11
[0.4.0]: #040---2026-10-10
[0.3.1]: #031---2026-10-10
[0.3.0]: #030---2026-10-09
[0.2.0]: #020---2026-10-09
[0.1.0]: #010---2026-10-08
