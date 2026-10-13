# Firmware updates and first-boot validation

Pumpkin installs updates that you upload in the clock's web page
(Device → Update firmware, `POST /api/update`). There is no update server and
nothing is installed automatically.

## How an update goes

1. The upload is written to the inactive application slot (`ota_0`/`ota_1`,
   1.75 MiB each) with the Arduino `Update` library. While it streams in,
   `ota_image_check` looks at it: the first chunk must be an ESP32 app image
   (magic `0xE9`, chip id ESP32), and by the end it must have contained this
   build's marker, `PUMPKIN-IMAGE|<hardware id>|` (`include/ota_image_check.h`).
   A file that fails is aborted and never selected; the error
   (`not_a_pumpkin_firmware`, `pumpkin_firmware_for_other_hardware`, …) shows
   on the Device page. The page runs the same check in the browser before it
   uploads anything, and `scripts/post_build.py` fails a build whose
   `firmware.bin` lacks the marker. The image is verified when the upload ends,
   and only then selected for the next boot.
2. The clock restarts into the new image. The bootloader starts it as
   *pending verification* (rollback is enabled in the Arduino-ESP32 2.0.17
   bootloader; `scripts/check_layout.py` checks `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE`).
3. `src/ota_boot_validation.cpp` overrides Arduino's weak `verifyRollbackLater()`
   hook (C linkage), so the image is **not** accepted before `setup()` runs.
4. `ota_manager` accepts it once the clock is healthy: at least 15 seconds
   running, configuration loaded, web assets present, more than 100 loop passes
   and at least 55,000 bytes of free heap. If only the heap is low, it waits up
   to 60 seconds for it to recover.
5. If the checks fail, or the new firmware restarts before they pass, the
   bootloader returns to the previous firmware. That firmware then reports
   "the last uploaded firmware failed its checks" on the Device page.

Avoid restarts or power loss in the first ~15 seconds after an update.

## Tests

- `tests/test_ota_image_check.py` feeds images through the upload check in
  every chunk size (marker split across chunks, other projects, other chips,
  other hardware), runs the update page's check against the same cases in Node,
  and checks that `post_build.py` looks for the same marker.

- `tests/test_ota_boot_validation.py` compiles the real `ota_boot_validation.cpp`
  against a weak C default and a C caller, so removing the override or C++
  name mangling fails the test. It also covers the timing, heap-recovery
  deadline and retry back-off.
- `tests/test_ota_boot_build_guard.py` covers `scripts/check_ota_boot_validation.py`,
  which `scripts/check_layout.py` runs after `pio run` against the real linker
  map and the installed Arduino core.

Based on SugarClock's OTA first-boot validation; the download, signing and
fleet parts were removed in Pumpkin 0.8.
