#!/usr/bin/env python3
"""PlatformIO post-build: the image fits an OTA slot and carries the marker
clocks check before installing an update."""
import os
import re
import sys

try:
    Import("env")  # noqa: F821 (PlatformIO/SCons)
    RUNNING_IN_PIO = True
except Exception:
    RUNNING_IN_PIO = False

ROOT = env.subst("$PROJECT_DIR") if RUNNING_IN_PIO else os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

SLOT = 0x1C0000  # partitions_custom.csv
WARN = int(SLOT * 0.85)


def image_marker(root=None):
    """The marker every Pumpkin image carries (src/core/image_check.h)."""
    header = open(os.path.join(root or ROOT, "src", "core", "image_check.h")).read()
    prefix = re.search(r'#define PUMPKIN_IMAGE_MARKER_PREFIX "([^"]+)"', header).group(1)
    hardware = re.search(r'#define PUMPKIN_HARDWARE_ID "([^"]+)"', header).group(1)
    return (prefix + hardware + "|").encode()


def check(path):
    data = open(path, "rb").read()
    size = len(data)
    print(f"[post-build] {size:,} of {SLOT:,} bytes ({size / SLOT * 100:.1f}% of an OTA slot)")
    if size > SLOT:
        sys.exit(f"[post-build] The image doesn't fit an OTA slot")
    if size > WARN:
        print("[post-build] Warning: over 85% of the OTA slot")
    marker = image_marker()
    if marker not in data:
        sys.exit(f"[post-build] {path} lacks the image marker {marker.decode()}; clocks would refuse it")
    print(f"[post-build] Image marker {marker.decode()}")


if RUNNING_IN_PIO:
    env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", lambda source, target, env: check(str(target[0])))  # noqa: F821
elif __name__ == "__main__" and len(sys.argv) > 1:
    check(sys.argv[1])
