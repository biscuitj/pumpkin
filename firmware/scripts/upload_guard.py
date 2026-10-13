#!/usr/bin/env python3
"""USB guard for `pio run -t upload | uploadfs | erase`.

PlatformIO picks a serial port by itself and esptool writes to whatever ESP32
answers there, so another developer's board on the same computer could be
flashed or wiped by accident. Before any target that writes to a device, this
script (a `post:` extra script, so it runs before anything is built):

1. picks the port: the one given with --upload-port / upload_port, or the
   single CH340 USB-serial device connected (the TC001's chip). None or more
   than one: stop and say which ports there are.
2. asks the device what it is (esptool, read-only): chip, MAC, flash size and
   the clock settings in its NVS, so it can tell a Pumpkin or SugarClock clock
   from anything else.
3. uploads to a Pumpkin/SugarClock clock without asking; asks y/N for an
   erase, or for a device it doesn't recognise. Without a terminal it refuses
   unless PUMPKIN_DEVICE lists the device's MAC.
4. fixes UPLOAD_PORT to the checked port, so PlatformIO can't pick another.

PUMPKIN_DEVICE=a4:cf:12:9b:e5:f6[,…]  MACs that need no confirmation.
PUMPKIN_SKIP_USB_GUARD=1             skip all of this (you're on your own).
"""
import os
import re
import shlex
import struct
import subprocess
import sys
import tempfile

try:
    Import("env")  # noqa: F821 (PlatformIO/SCons)
    RUNNING_IN_PIO = True
except Exception:
    RUNNING_IN_PIO = False

# Ulanzi TC001: CH340 (WCH). 55D4 is the CH9102, in case a batch ships it.
CH340_IDS = {(0x1A86, 0x7523), (0x1A86, 0x55D4)}
NVS_OFFSET, NVS_SIZE = 0x9000, 0x5000          # partitions_custom.csv
MIN_FLASH_MB = 4
CLOCK_NAMESPACE = "tc001cfg"                     # config_manager.cpp
CLOCK_NAME_KEY = "clk_name"                      # Pumpkin only (not SugarClock)
PROBE_TIMEOUT_S = 45


def say(text=""):
    print(f"[PUMPKIN] {text}" if text else "", flush=True)


# ---------------------------------------------------------------------------
# Pure helpers (unit-tested in tests/test_upload_guard.py)

def device_targets(targets):
    """The command-line targets that write to or erase a device."""
    return [t for t in targets if t.startswith("upload") or "erase" in t]


def normalize_mac(mac):
    mac = re.sub(r"[^0-9a-fA-F]", "", mac or "").lower()
    return ":".join(mac[i:i + 2] for i in range(0, 12, 2)) if len(mac) == 12 else ""


def allowed_macs(value):
    return {m for m in (normalize_mac(part) for part in (value or "").split(",")) if m}


def candidate_ports(ports):
    """CH340 ports, one per physical device.

    macOS can list one adapter twice (Apple's driver and WCH's: usbserial and
    wchusbserial), so ports on the same USB location count once.
    """
    chosen = {}
    for port in ports:
        if (port.vid, port.pid) not in CH340_IDS:
            continue
        key = port.location or port.serial_number or port.device
        current = chosen.get(key)
        # Prefer /dev/cu.* (macOS call-out device) over /dev/tty.*.
        if current is None or ("/cu." in port.device and "/cu." not in current.device):
            chosen[key] = port
    return sorted(chosen.values(), key=lambda p: p.device)


def describe_port(port):
    ids = f"{port.vid:04X}:{port.pid:04X}" if port.vid is not None else "no USB id"
    return f"{port.device}  ({port.description or 'unknown'}, {ids})"


def parse_esptool_info(text):
    info = {"mac": "", "chip": "", "flash_mb": None}
    m = re.search(r"\bMAC:\s*([0-9a-fA-F]{2}(?::[0-9a-fA-F]{2}){5})", text)
    if m:
        info["mac"] = normalize_mac(m.group(1))
    m = re.search(r"Chip (?:is|type:)\s*([A-Za-z0-9-]+)", text)
    if m:
        info["chip"] = m.group(1)
    m = re.search(r"Detected flash size:\s*(\d+)\s*MB", text)
    if m:
        info["flash_mb"] = int(m.group(1))
    return info


NVS_PAGE, NVS_ENTRY, NVS_FIRST_ENTRY, NVS_ENTRIES = 4096, 32, 64, 126
NVS_PAGE_IN_USE = {0xFFFFFFFE, 0xFFFFFFFC, 0xFFFFFFF8}   # active, full, freeing
NVS_WRITTEN = 0b10
NVS_TYPE_U8, NVS_TYPE_STR = 0x01, 0x21


def parse_nvs(data):
    """Namespaces and (namespace, key) -> value for U8 and string entries.

    Enough of the ESP-IDF NVS format to read a few settings; anything that
    doesn't look right is skipped rather than trusted.
    """
    pages = []
    for offset in range(0, len(data) - NVS_PAGE + 1, NVS_PAGE):
        page = data[offset:offset + NVS_PAGE]
        state, seq = struct.unpack_from("<II", page, 0)
        if state in NVS_PAGE_IN_USE:
            pages.append((seq, page))
    pages.sort(key=lambda item: item[0])

    namespaces, values = {}, {}
    for _, page in pages:
        bitmap = page[32:64]
        i = 0
        while i < NVS_ENTRIES:
            state = (bitmap[i // 4] >> ((i % 4) * 2)) & 0b11
            if state != NVS_WRITTEN:
                i += 1
                continue
            start = NVS_FIRST_ENTRY + i * NVS_ENTRY
            entry = page[start:start + NVS_ENTRY]
            ns, kind, span = entry[0], entry[1], entry[2]
            if span == 0 or i + span > NVS_ENTRIES:
                i += 1
                continue
            key = entry[8:24].split(b"\0", 1)[0].decode("ascii", "replace")
            if kind == NVS_TYPE_U8:
                if ns == 0:
                    namespaces[entry[24]] = key
                else:
                    values[(ns, key)] = entry[24]
            elif kind == NVS_TYPE_STR and ns != 0:
                size = struct.unpack_from("<H", entry, 24)[0]
                body = page[start + NVS_ENTRY:start + span * NVS_ENTRY][:size]
                values[(ns, key)] = body.split(b"\0", 1)[0].decode("utf-8", "replace")
            i += span
    named = {(namespaces[ns], key): v for (ns, key), v in values.items() if ns in namespaces}
    return set(namespaces.values()), named


def identify(nvs_data, mac):
    """('pumpkin' | 'sugarclock' | 'unknown', description)."""
    try:
        namespaces, values = parse_nvs(nvs_data or b"")
    except Exception:
        namespaces, values = set(), {}
    if CLOCK_NAMESPACE not in namespaces:
        return "unknown", "no Pumpkin or SugarClock settings on it"
    if (CLOCK_NAMESPACE, CLOCK_NAME_KEY) in values:
        name = values[(CLOCK_NAMESPACE, CLOCK_NAME_KEY)]
        if not name and mac:
            name = "pumpkin-" + mac.replace(":", "")[-4:]   # clock_default_name()
        return "pumpkin", f'Pumpkin clock "{name}"' if name else "Pumpkin clock"
    return "sugarclock", "SugarClock clock (or Pumpkin before 0.7)"


def decide(targets, kind, mac, allowed, flash_mb):
    """'go', 'ask', or a reason to stop."""
    if flash_mb is not None and flash_mb < MIN_FLASH_MB:
        return f"stop: {flash_mb} MB flash, a TC001 has at least {MIN_FLASH_MB} MB"
    if mac and mac in allowed:
        return "go"
    erase = any("erase" in t for t in targets)
    if kind in ("pumpkin", "sugarclock") and not erase:
        return "go"
    return "ask"


def esptool_command(uploader, objcopy, python):
    """How to run the platform's esptool: $UPLOADER, else $OBJCOPY (both are
    esptool.py in the espressif32 platform), else `python -m esptool`."""
    for tool in (uploader, objcopy):
        tool = (tool or "").strip()
        if not tool:
            continue
        # A plain path (may contain spaces or Windows backslashes) or a command line.
        parts = [tool] if os.path.exists(tool) else shlex.split(tool, posix=os.name != "nt")
        if not any("esptool" in os.path.basename(part).lower() for part in parts):
            continue
        return [python] + parts if parts[0].lower().endswith(".py") else parts
    return [python, "-m", "esptool"]


# ---------------------------------------------------------------------------
# PlatformIO side

def stop(message, hint=None):
    say()
    say("USB guard: " + message)
    if hint:
        for line in hint.splitlines():
            say("  " + line)
    say("Nothing was written. (Bypass at your own risk: PUMPKIN_SKIP_USB_GUARD=1.)")
    say()
    sys.exit(1)


def run_esptool(base, port, baud, mcu, args):
    cmd = base + ["--chip", mcu, "--port", port, "--baud", str(baud),
                  "--before", "default_reset", "--after", "hard_reset"] + args
    try:
        result = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                text=True, timeout=PROBE_TIMEOUT_S)
    except FileNotFoundError:
        stop(f"couldn't run esptool ({cmd[0]}).")
    except subprocess.TimeoutExpired:
        stop(f"the device on {port} didn't answer in {PROBE_TIMEOUT_S} s.",
             "Hold the clock's middle button while plugging in the cable, then try again.")
    if result.returncode != 0:
        tail = "\n".join(result.stdout.strip().splitlines()[-6:])
        stop(f"couldn't read the device on {port}.",
             tail + "\nIs it the clock? Hold its middle button while plugging in the cable, then try again.")
    return result.stdout


def ask(question):
    if not (sys.stdin and sys.stdin.isatty()):
        return None
    say(question + " [y/N]")
    try:
        answer = sys.stdin.readline()
    except Exception:
        return False
    return answer.strip().lower() in ("y", "yes")


def guard(env, targets):
    writes = device_targets(targets)
    if not writes:
        return
    if os.environ.get("PUMPKIN_SKIP_USB_GUARD") == "1":
        say("USB guard skipped (PUMPKIN_SKIP_USB_GUARD=1).")
        return

    from serial.tools import list_ports
    ports = list(list_ports.comports())
    port = env.subst("$UPLOAD_PORT").strip()
    if port:
        say(f"USB guard: using the port you chose, {port}")
    else:
        found = candidate_ports(ports)
        listing = "\n".join(describe_port(p) for p in ports) or "(no serial ports at all)"
        if not found:
            stop("no Ulanzi clock (CH340 USB serial) is connected.",
                 "Serial ports right now:\n" + listing +
                 "\nUse a USB data cable, directly on the computer. If the clock is one of\n"
                 "the ports above, choose it: pio run -t " + writes[0] + " --upload-port PORT")
        if len(found) > 1:
            stop("more than one CH340 device is connected, so it's not clear which one is the clock.",
                 "\n".join(describe_port(p) for p in found) +
                 "\nUnplug the others, or choose one: pio run -t " + writes[0] + " --upload-port PORT")
        port = found[0].device

    mcu = env.BoardConfig().get("build.mcu", "esp32")
    baud = env.subst("$UPLOAD_SPEED") or "115200"
    base = esptool_command(env.subst("$UPLOADER"), env.subst("$OBJCOPY"), env.subst("$PYTHONEXE"))
    say(f"USB guard: checking the device on {port} (read only) …")
    info = parse_esptool_info(run_esptool(base, port, baud, mcu, ["flash_id"]))
    with tempfile.TemporaryDirectory() as tmp:
        dump = os.path.join(tmp, "nvs.bin")
        run_esptool(base, port, baud, mcu, ["read_flash", hex(NVS_OFFSET), hex(NVS_SIZE), dump])
        with open(dump, "rb") as fp:
            nvs = fp.read()
    kind, what = identify(nvs, info["mac"])
    flash = f"{info['flash_mb']} MB flash" if info["flash_mb"] else "flash size unknown"
    say(f"  {info['chip'] or mcu.upper()}, MAC {info['mac'] or 'unknown'}, {flash}")
    say(f"  {what}")

    verdict = decide(writes, kind, info["mac"], allowed_macs(os.environ.get("PUMPKIN_DEVICE")),
                     info["flash_mb"])
    if verdict.startswith("stop:"):
        stop(f"the device on {port} doesn't look like a TC001 ({verdict[6:]}).")
    if verdict == "ask":
        action = "ERASE everything on" if any("erase" in t for t in writes) else "Flash Pumpkin onto"
        answer = ask(f"{action} this device ({info['mac'] or port})?")
        if answer is None:
            stop("this needs a confirmation and there's no terminal to ask in.",
                 f"Run it in a terminal, or confirm this device up front:\n"
                 f"PUMPKIN_DEVICE={info['mac'] or 'MAC'} pio run -t {' -t '.join(writes)}")
        if not answer:
            stop("not confirmed.")
    env.Replace(UPLOAD_PORT=port)
    say(f"USB guard: {', '.join(writes)} → {port}")


if RUNNING_IN_PIO:
    from SCons.Script import COMMAND_LINE_TARGETS  # noqa: E402
    guard(env, list(COMMAND_LINE_TARGETS))  # noqa: F821
