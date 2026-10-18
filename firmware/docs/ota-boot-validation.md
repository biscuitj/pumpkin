# Firmware updates and first-boot validation

Pumpkin installs updates that you upload in the dashboard (Device → Firmware,
`POST /api/update` with the `.bin` as the body). There is no update server and
nothing is installed automatically.

## How an update goes

1. The dashboard checks the file in the browser first (`dashboard/src/lib/firmware.ts`).
2. The clock writes it to the inactive application slot (`ota_0`/`ota_1`,
   1.75 MiB each) with the Arduino `Update` library. While it streams in,
   `src/core/image_check.cpp` looks at it: the first bytes must be an ESP32 app
   image (magic `0xE9`, chip id ESP32), and by the end it must have contained
   this build's marker, `PUMPKIN-IMAGE|<hardware id>|` (`src/core/image_check.h`).
   A file that fails is aborted and never selected; the dashboard shows why
   (`not_firmware`, `wrong_chip`, `not_pumpkin`, `wrong_hardware`, `truncated`).
   `scripts/post_build.py` fails a build whose `firmware.bin` lacks the marker.
3. The clock restarts into the new image. The bootloader starts it as
   *pending verification* (rollback is enabled in the Arduino-ESP32 2.0.17
   bootloader; `scripts/check_layout.py` checks `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE`).
4. `src/hal/ota.cpp` overrides Arduino's weak `verifyRollbackLater()` hook
   (C linkage), so the image is **not** accepted before `setup()` runs.
5. `otaLoop()` accepts it once the clock is healthy: at least 15 seconds
   running, settings loaded, the dashboard embedded, more than 100 loop passes
   and at least 55,000 bytes of free heap. If only the heap is low, it waits up
   to 60 seconds for it to recover.
6. If the checks fail, or the new firmware restarts before they pass, the
   bootloader returns to the previous firmware. That one then reports the
   rollback (with the version that failed) on the Device tab.

Avoid restarts or power loss in the first ~15 seconds after an update.

State lives in the NVS namespace `sugarota` (kept from SugarClock): `pending`
and `previous` while an update is out, `next` (the new version, written on its
first start) and `last_error`.

`scripts/check_ota_boot_validation.py` (run by `check_layout.py` after
`pio run`) checks the installed Arduino core still gates acceptance on the hook,
and that the linker map has exactly one live definition, from `src/hal/ota.cpp`.
