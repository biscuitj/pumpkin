#!/usr/bin/env python3
"""Fail builds when version/partition/rollback invariants drift."""

import csv
import os
import re
import sys

from check_ota_boot_validation import check_ota_boot_validation

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EXPECTED = {
    "nvs": ("0x9000", "0x5000"),
    "otadata": ("0xe000", "0x2000"),
    "ota_0": ("0x10000", "0x1C0000"),
    "ota_1": ("0x1D0000", "0x1C0000"),
    "spiffs": ("0x390000", "0x70000"),
}


def read(path):
    with open(os.path.join(ROOT, path), encoding="utf-8") as stream:
        return stream.read()


def fail(message):
    print(f"layout check failed: {message}", file=sys.stderr)
    raise SystemExit(1)


rows = {}
with open(os.path.join(ROOT, "partitions_custom.csv"), newline="", encoding="utf-8") as stream:
    for row in csv.reader(line for line in stream if not line.lstrip().startswith("#")):
        if not row or not row[0].strip():
            continue
        rows[row[0].strip()] = (row[3].strip(), row[4].strip())
if rows != EXPECTED:
    fail(f"partition table differs: {rows!r}")
if int(rows["spiffs"][0], 16) + int(rows["spiffs"][1], 16) != 0x400000:
    fail("partition table does not end at 4 MiB")

version = read("VERSION").strip()
if not re.fullmatch(r"(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)", version):
    fail("VERSION is not strict major.minor.patch")
if "PUMPKIN_VERSION" not in read("src/main.cpp") or '#define FIRMWARE_VERSION' in read("src/main.cpp"):
    fail("firmware does not consume the authoritative injected version")

firmware = os.path.join(ROOT, ".pio", "build", "esp32dev", "firmware.bin")
if os.path.exists(firmware) and os.path.getsize(firmware) > 0x1C0000:
    fail("firmware.bin exceeds OTA slot")
if os.path.exists(firmware):
    try:
        check_ota_boot_validation(
            os.path.expanduser("~/.platformio/packages/framework-arduinoespressif32/cores/esp32/esp32-hal-misc.c"),
            os.path.join(ROOT, ".pio", "build", "esp32dev", "firmware.map"))
    except (OSError, ValueError) as error:
        fail(str(error))
    print("OTA boot-validation check passed: Arduino hook and live application definition")
else:
    print("SKIPPED OTA boot-validation check: firmware.bin is missing; run pio run first")

sdkconfig = os.path.expanduser("~/.platformio/packages/framework-arduinoespressif32/tools/sdk/esp32/sdkconfig")
if os.path.exists(sdkconfig) and "CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y" not in open(sdkconfig, encoding="utf-8").read():
    fail("resolved ESP32 bootloader does not enable app rollback")

print(f"layout checks passed for v{version}")
