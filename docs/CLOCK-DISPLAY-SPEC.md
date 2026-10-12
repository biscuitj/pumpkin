# Pumpkin: display spec (bridge features)

All of this is in the firmware since 0.6.0. Items marked "next round" were
built in 0.6.0; see `CHANGELOG.md` for later changes.

## Screens

### Glucose, with sport mode (done)
- **Sport mode:** while the temp target is active (`temp_target: true`), the glucose screen gets a 2 px wide, full-height bar on each side, columns 0–1 and 30–31.
- **Colour and brightness:** the bars are in the value's colour, at the normal brightness.
- **Old data:** no bars when the data is stale or in the stale warning (10–20 min). The "!" uses the right edge then.
- **Urgent low:** keeps priority as before; the bars stay on.

### IOB (done)
- `0.35U` in cyan, centred: 2 decimals below 10 U, 1 decimal from 10 U.
- `--U` in grey when IOB is `null`, or when the data is stale.
- Its own screen after TIR, which can be turned off like Trend, TIR and Time: "Show Active Insulin Screen" (on by default), stored as `iob_en`.

### Alerts (done; next round in **bold**)
A separate screen that only appears in the rotation while there is something to show. The Worker sends `warnings`, already deduplicated and in this order:

| id | when (Worker) | shown as | duration |
|---|---|---|---|
| **`suspended`** | **pump stopped by hand: banner `DELIVERY_SUSPEND`, no auto-suspend** | **two orange (0xFF6D00) 2×6 bars, 2 px gap, rows 1–6** | **5 s** |
| `warning` | pump alerts delivery stopped, set connect, sensor signal, calibration, **and SmartGuard auto-suspend (809/810, `SUSPENDED_ON_LOW` / `SUSPENDED_BEFORE_LOW`)** | amber triangle (0xFBBC04) with an unlit 2 px exclamation mark | 5 s |
| `battery` | pump battery alert (remembered until the battery % goes up) | battery · **next: light blue 0x64B5F6** (now red) | 5 s |
| `reservoir` | reservoir alert (remembered until the reservoir goes up ≥ 5 U) | reservoir · **next: green 0x34A853** (now red) | 5 s |
| `bg` | BG required (remembered until a finger-prick BG, or until SmartGuard is back in Auto Basal) | red blood drop | 5 s |
| `sensor` | sensor alert (remembered until sensor time left goes up ≥ 12 h) | done: `SENSOR VERVANGEN` scrolls, red · **next: static `SENSOR` in narrow 4×7 letters, yellow 0xFFE000** | scroll once · **5 s** |
| `set` | set change reminder (remembered until a tubing fill: suspended + reservoir −≥ 5 U unexplained) | done: `SET VERVANGEN` scrolls, red · **next: static `SET` in the normal font, yellow 0xFFE000** | scroll once · **5 s** |

How the screen behaves:
- **Several alerts:** they play one after another, then the rotation moves on, whatever the cycle time is.
- **Coming back:** they play again when the rotation comes round.
- **Without auto-cycle:** they loop until you press next.
- **Remembered alerts:** they stay after being dismissed on the pump, until the action shows in the pump data, 72 h pass, or they're cleared by hand.
- **Not remembered:** `suspended` and `warning` show only while they're active on the pump.
- **Next round:** the screen keeps its place by alert type rather than by position, so no alert is skipped when another one clears.

### Preview and clearing (web UI)
- **Done:** preview buttons Battery, Reservoir, BG required, Warning, Sensor, Set and All (`POST /api/test/alert`). They never override an urgent low.
- **Next round:**
  - a **Pause** preview button;
  - **Clear pump alerts**: the clock calls the Worker (`/reminders/clear?id=all`) from its main loop, then fetches straight away. It clears remembered reminders; alerts the pump still shows as active stay until they're dismissed on the pump.

### Night mode: Full Mute (next round)
Meant for a clock at school, outside school hours.
- **Setting:** a new option, "Full Mute", next to the existing night mode settings (start hour, end hour, night brightness). It only does anything while night mode is on.
- **Behaviour:** during the night hours, the matrix is fully black instead of dimmed to `night_brightness`, and the buzzer stays silent.
- **Nothing wakes it:** not urgent low or high, not pump alerts, not a button press.
- **How:** the brightness goes to 0 inside `effective_brightness()`, so every screen goes dark the same way. The stale screens' minimum of 20 must not apply while muted. The existing API clamps `night_brightness` to 1–255, which is why a separate option is needed.
- **Fetching carries on,** so the right value is there the moment the night hours end.
- **Without a valid time** (no NTP yet), night mode is off, as today, so the clock never goes dark by mistake.
- **Storage:** NVS key `night_mute`, JSON key `night_full_mute`, off by default. It's included in `/api/config` GET/POST and in both index.html copies.

## Rotation order

```
glucose → trend → TIR → IOB → time → weather → pet → ALERTS → timer → …
```

- **Position:** the alerts screen sits after the pet, or where the pet would be if the pet screen is off.
- **"Pump Alerts Replace the Pet Screen"** (off by default): with alerts active, they take the pet's slot.
- **Turning it off:** "Show Pump Alerts Screen" (on by default) switches the alerts screen off entirely.

## Settings (web UI + NVS)

| setting | key | default |
|---|---|---|
| Show Active Insulin Screen | `iob_en` | on |
| Show Pump Alerts Screen | `alrt_en` | on |
| Pump Alerts Replace the Pet Screen | `alrt_pet` | off |
| **Full Mute** (night mode, next round) | `night_mute` | off |
