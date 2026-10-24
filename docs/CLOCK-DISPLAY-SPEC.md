# Pumpkin: what the clock shows

The display as firmware 1.0 draws it on the 32×8 matrix. The code is
`firmware/src/core/engine.cpp` (screens, rotation, overlays, brightness),
`pets.cpp` and `art.cpp`; the dashboard's Display tab mirrors the matrix live.
Settings named here are on the dashboard's **Display**, **Pet** and
**Schedule** tabs.

## Screens

### Glucose (always on)
- The value in its range colour, with the trend arrow right after it, centred
  together. mg/dL or mmol/L (one decimal).
- **Range colours** come from one of three colour sets:

  | set | urgent low | low | in range | high | urgent high | old |
  |---|---|---|---|---|---|---|
  | classic | red EA4335 | amber FBBC04 | green 34A853 | amber FBBC04 | red EA4335 | grey 808080 |
  | colour-blind friendly | FF3B1F | orange FF9F00 | blue 2D9CFF | purple B57BFF | magenta E040FB | grey 808080 |
  | calm | FF3020 | FFB000 | white E8E8E8 | FFB000 | FF3020 | grey 606060 |

  Defaults: urgent low 55, low 70, high 180, urgent high 250 mg/dL.
- **Getting old:** from half the stale time (default 10 of 20 min) a yellow `!`
  sits at the right edge and the screen dims to a third (never below 20).
- **Old:** after the stale time (10–60 min, default 20), or 5 failed fetches in
  a row, the value turns grey.
- **No reading yet:** `---` in grey.
- **Sport mode:** while a temp target is on (`temp_target`), 2 px bars in the
  value's colour on columns 0–1 and 30–31. Not when the reading is getting old
  (the `!` needs the edge).
- **Delta flash** (on by default): for 3 s after a new reading, the change since
  the previous one (`+12`, `-0.4`) instead of the value. Not on old data, never
  during the low lock.

### Bridge messages
- `bridge_alert` (`CARELINK LOGIN NEEDED`, `CARELINK ERROR`) scrolls in amber,
  right after the glucose, while the bridge sends it.
- `message` scrolls in white in the same place.
- Each scrolls through at least once before the rotation moves on.

### Time in range
Right-aligned `78%`: in-range colour from 70 %, high colour from 50 %, urgent
low colour below that. `--%` in grey when unknown (`-1`) or the data is old.

### Active insulin
`0.35U` in cyan, centred: two decimals below 10 U, one from 10 U. `--U` in grey
when unknown or old.

### Time
`14:05` in white with a blinking colon, 24-hour or 12-hour. With a date format
set, time and date (`18/10` or `10/18`) take turns every 5 s. `--:--` until the
clock has the time (DS1307, NTP, or the bridge's `Date` header).

### Pet
One of eight pets: Patch the pumpkin (default), Toffee the cat, Kiko the frog,
Domino the penguin, Hoot the owl, Leo the lion, Stella the unicorn, Kobi the
crab.
- **Pose from the reading:** happy in range, droopy when low, agitated when
  high. Asleep (eyes closed, a drifting `z`) outside the awake hours, when
  the schedule is on.
- **Next to the pet:** `LOW` / `OK` / `HIGH` (or the time in range instead), a
  three-step range icon, or nothing with the pet centred.
- **Waves** now and then by itself, and when you press the middle button on
  this screen.
- **Seasonal surprises** (pet only, in range): a Halloween touch on 31 October,
  fireworks on 31 December and 1 January.
- **No reading, or old data:** `---` instead of a pose. An urgent value never
  takes the pet's place: the pet shows the range it's in.

### Pump alerts
Only in the rotation while the bridge sends `warnings` and the data isn't old.
Each alert shows for 5 s, highest priority first:

| id | shown as |
|---|---|
| `suspended` | two orange (FF6D00) pause bars |
| `warning` | amber (FBBC04) triangle |
| `battery` | light blue (64B5F6) battery |
| `reservoir` | green (34A853) reservoir |
| `bg` | red (EA4335) blood drop |
| `sensor_change` | `! SENSOR !` in narrow letters, red (EA4335): the pump's replace-sensor alert |
| `sensor` | `SENSOR` in narrow letters, yellow (FFE000): the sensor runs out within a few hours; not shown next to `sensor_change` |
| `set` | `SET`, yellow (FFE000) |

- Every alert plays once, then the rotation moves on. Without auto-cycle they
  keep looping.
- Played alerts are tracked by type, so one that clears never makes another one
  skip its turn.
- **Clear remembered alerts** (Display tab) asks the bridge to forget
  remembered reminders (`POST /reminders/clear?id=all`); alerts the pump still
  shows stay.

## Rotation

```
glucose → bridge alert → message → time in range → active insulin → time → pet → pump alerts
```

- Every screen but glucose can be switched off. Bridge alerts, messages and pump
  alerts only appear while there's something to show.
- **Pump alerts replace the pet** (off by default): with alerts active, they
  take the pet's place instead of following it.
- **Auto-cycle** (on by default): each screen shows for 3–300 s (default 10).
  Off: only the buttons change screens.
- Screens fade out and in over 600 ms.

## Above the rotation

Highest priority first:

1. **Update:** progress in %, then `REBOOT`, in teal.
2. **Boot:** the pixel pumpkin, then `Pumpkin` scrolls by (about 4 s).
3. **Low lock** (on by default): a fresh reading below *low* pins the glucose
   screen. No fades, buttons, previews or address while it lasts; the
   dashboard says so under its mirror and greys out previous/next. Once the
   reading is back above *low*, glucose gets a full turn and the rotation
   goes on from there.
4. **Previews** from the dashboard: a pet pose, glucose value or the alerts
   (each alert once). Never over an urgent value.
5. **Address:** `Visit keuken.local or 192.168.0.12` (hold the middle button,
   or after joining Wi-Fi from the setup page).
6. **Status messages:**
   - setup hotspot: `Wi-Fi setup: join Pumpkin-Setup-E5F6, then open http://192.168.4.1`,
     and while joining: `Joining …`, `Signing in to …`, or why it failed (teal);
   - `NO` / `WIFI` (red, alternating) when the saved network is gone;
   - the network blocks DNS or the bridge (amber, names the host);
   - `Visit … to set up` when no bridge is set (teal);
   - `NO` / `DATA` (red) without data, or after 10 failed fetches.

Demo data skips the status messages: it needs nothing from the network.

## Brightness and night

- **Auto** (default): from the light sensor, 5 in a dark room to 200 in
  daylight. **Manual:** a fixed level.
- **Schedule:** awake hours per weekday (in minutes). Outside them the clock
  either **dims** to the night brightness or goes **dark**. Dark means fully
  black: nothing wakes it, not a low, not an alert, not a button. Fetching
  carries on.
- Without a valid time the clock counts as awake, so it never goes dark by
  mistake.
- Old readings never go below brightness 20, so they stay readable.

## Buttons

| Button | Press | Hold (1 s) |
|---|---|---|
| Left | previous screen | |
| Middle | the pet waves (on the pet screen) | show the address |
| Right | next screen | |
