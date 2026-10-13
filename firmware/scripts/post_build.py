#!/usr/bin/env python3
import os
import re
import sys

try:
    Import("env")
    RUNNING_IN_PIO = True
except Exception:
    RUNNING_IN_PIO = False

ROOT_DIR = (env.subst("$PROJECT_DIR") if RUNNING_IN_PIO
            else os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

MAX_SLOT_SIZE = 0x1C0000 # 1,835,008 bytes (1.75 MiB)
WARN_THRESHOLD = int(MAX_SLOT_SIZE * 0.85) # 1,559,756 bytes (85%)

def check_firmware_size(source, target, env):
    firmware_path = str(target[0])
    if not os.path.exists(firmware_path):
        return

    size = os.path.getsize(firmware_path)
    pct = (size / MAX_SLOT_SIZE) * 100.0
    print(f"[POST-BUILD] Firmware size: {size:,} bytes / {MAX_SLOT_SIZE:,} bytes ({pct:.1f}% of OTA slot)")

    if size > MAX_SLOT_SIZE:
        print(f"[ERROR] Firmware size {size:,} bytes exceeds maximum OTA slot size {MAX_SLOT_SIZE:,} bytes!")
        sys.exit(1)
    elif size > WARN_THRESHOLD:
        print(f"[WARNING] Firmware size is at {pct:.1f}% of OTA slot capacity. High slot utilization!")

def image_marker(root=None):
    """The marker every Pumpkin image carries (include/ota_image_check.h)."""
    header = open(os.path.join(root or ROOT_DIR, "include", "ota_image_check.h")).read()
    prefix = re.search(r'#define PUMPKIN_IMAGE_MARKER_PREFIX "([^"]+)"', header).group(1)
    hardware = re.search(r'#define PUMPKIN_HARDWARE_ID "([^"]+)"', header).group(1)
    return (prefix + hardware + "|").encode()

def check_image_marker(source, target, env):
    # A clock only installs an upload that contains this marker, so a build
    # without it could never be installed through the web page.
    firmware_path = str(target[0])
    if not os.path.exists(firmware_path):
        return
    marker = image_marker()
    with open(firmware_path, "rb") as fp:
        data = fp.read()
    if marker not in data:
        print(f"[ERROR] {firmware_path} doesn't contain the image marker {marker.decode()}; "
              "clocks would refuse it as an update.")
        sys.exit(1)
    print(f"[POST-BUILD] Image marker: {marker.decode()}")

if RUNNING_IN_PIO:
    env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", check_firmware_size)
    env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", check_image_marker)
