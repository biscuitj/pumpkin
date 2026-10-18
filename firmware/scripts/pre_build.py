#!/usr/bin/env python3
"""PlatformIO pre-build: the version from VERSION, and the dashboard embedded."""
import os
import re
import sys

try:
    Import("env")  # noqa: F821 (PlatformIO/SCons)
    RUNNING_IN_PIO = True
except Exception:
    RUNNING_IN_PIO = False

ROOT = env.subst("$PROJECT_DIR") if RUNNING_IN_PIO else os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def version():
    path = os.path.join(ROOT, "VERSION")
    ver = open(path).read().strip()
    if not re.fullmatch(r"(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)", ver):
        raise ValueError(f"VERSION '{ver}' isn't major.minor.patch")
    return ver


ver = version()
print(f"[pre-build] Pumpkin {ver}")

sys.path.insert(0, os.path.join(ROOT, "scripts"))
from embed_dashboard import embed  # noqa: E402

embed(ROOT)

if RUNNING_IN_PIO:
    env.Append(CPPDEFINES=[("PUMPKIN_VERSION", f'\\"{ver}\\"')])  # noqa: F821
