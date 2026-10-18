"""USB guard: which port, what device, and when to ask before upload/erase."""
import importlib.util
from pathlib import Path
import struct
from types import SimpleNamespace
import unittest

spec = importlib.util.spec_from_file_location(
    "upload_guard", Path(__file__).resolve().parents[1] / "scripts/upload_guard.py")
guard = importlib.util.module_from_spec(spec)
spec.loader.exec_module(guard)


def port(device, vid=0x1A86, pid=0x7523, location=None, serial=None, description="USB Serial"):
    return SimpleNamespace(device=device, vid=vid, pid=pid, location=location,
                           serial_number=serial, description=description)


def nvs_entry(ns, kind, key, data=b"", span=1):
    entry = bytes([ns, kind, span, 0xFF]) + b"\0" * 4 + key.encode().ljust(16, b"\0")
    return entry + data.ljust(8, b"\xff")[:8]


def nvs_page(entries, seq=0, state=0xFFFFFFFE):
    """One NVS page from (ns, kind, key, value) tuples; strings get data entries."""
    slots, states = [], []
    for ns, kind, key, value in entries:
        if kind == guard.NVS_TYPE_STR:
            raw = value.encode() + b"\0"
            body = raw.ljust(((len(raw) + 31) // 32) * 32, b"\xff")
            span = 1 + len(body) // 32
            slots.append(nvs_entry(ns, kind, key, struct.pack("<HHI", len(raw), 0xFFFF, 0), span))
            slots += [body[i:i + 32] for i in range(0, len(body), 32)]
            states += [guard.NVS_WRITTEN] * span
        else:
            slots.append(nvs_entry(ns, kind, key, bytes([value])))
            states.append(guard.NVS_WRITTEN)
    bitmap = bytearray(b"\xff" * 32)
    for i, st in enumerate(states):
        bitmap[i // 4] &= ~(0b11 << ((i % 4) * 2)) & 0xFF
        bitmap[i // 4] |= st << ((i % 4) * 2)
    header = struct.pack("<II", state, seq) + b"\xfe" + b"\xff" * 23
    body = b"".join(slots)
    return (header + bytes(bitmap) + body).ljust(4096, b"\xff")


def nvs(*pages):
    data = b"".join(pages)
    return data + b"\xff" * (guard.NVS_SIZE - len(data))


PUMPKIN_NVS = nvs(nvs_page([
    (0, 0x01, "sugarota", 1),
    (0, 0x01, "tc001cfg", 2),
    (2, 0x01, "brightness", 40),
    (2, 0x21, "clk_name", "Emilie"),
    (2, 0x21, "bg_url", "https://pumpkin-bridge.example.workers.dev/clock"),
]))


class PortTests(unittest.TestCase):
    def test_only_ch340_ports_one_per_device(self):
        ports = [
            port("/dev/cu.Bluetooth-Incoming-Port", vid=None, pid=None),
            port("/dev/cu.SLAB_USBtoUART", vid=0x10C4, pid=0xEA60),       # CP2102 dev board
            port("/dev/cu.usbmodem101", vid=0x303A, pid=0x1001),           # ESP32-S3 native USB
            port("/dev/cu.usbserial-1410", location="20-1"),
            port("/dev/tty.usbserial-1410", location="20-1"),
            port("/dev/cu.wchusbserial1410", location="20-1"),
        ]
        found = guard.candidate_ports(ports)
        self.assertEqual(len(found), 1)
        self.assertTrue(found[0].device.startswith("/dev/cu."))

    def test_two_clocks_are_two_candidates(self):
        found = guard.candidate_ports([port("/dev/ttyUSB0", location="1-1"),
                                       port("/dev/ttyUSB1", location="1-2", pid=0x55D4)])
        self.assertEqual([p.device for p in found], ["/dev/ttyUSB0", "/dev/ttyUSB1"])

    def test_without_location_ports_stay_apart(self):
        found = guard.candidate_ports([port("COM3"), port("COM4")])
        self.assertEqual(len(found), 2)


class TargetTests(unittest.TestCase):
    def test_device_targets(self):
        self.assertEqual(guard.device_targets(["upload", "monitor"]), ["upload"])
        self.assertEqual(guard.device_targets(["erase"]), ["erase"])
        self.assertEqual(guard.device_targets(["uploadfs", "erase_upload"]), ["uploadfs", "erase_upload"])
        self.assertEqual(guard.device_targets([]), [])
        self.assertEqual(guard.device_targets(["buildfs", "size", "compiledb"]), [])


class EsptoolTests(unittest.TestCase):
    V4 = """esptool.py v4.7.0
Serial port /dev/cu.usbserial-1410
Connecting....
Chip is ESP32-D0WD-V3 (revision v3.1)
Features: WiFi, BT, Dual Core, 240MHz
Crystal is 40MHz
MAC: a4:cf:12:9b:e5:f6
Uploading stub...
Manufacturer: 20
Device: 4016
Detected flash size: 8MB
Hard resetting via RTS pin...
"""
    V5 = """esptool v5.0.0
Connected to ESP32 on /dev/ttyUSB0:
Chip type:          ESP32-D0WD-V3 (revision v3.1)
MAC:                A4:CF:12:9B:E5:F6
Detected flash size: 4MB
"""

    def test_parses_v4_and_v5(self):
        self.assertEqual(guard.parse_esptool_info(self.V4),
                         {"mac": "a4:cf:12:9b:e5:f6", "chip": "ESP32-D0WD-V3", "flash_mb": 8})
        self.assertEqual(guard.parse_esptool_info(self.V5),
                         {"mac": "a4:cf:12:9b:e5:f6", "chip": "ESP32-D0WD-V3", "flash_mb": 4})
        self.assertEqual(guard.parse_esptool_info("garbage"), {"mac": "", "chip": "", "flash_mb": None})

    def test_esptool_command(self):
        py = "/venv/bin/python"
        self.assertEqual(guard.esptool_command("/pkgs/tool-esptoolpy/esptool.py", "", py),
                         [py, "/pkgs/tool-esptoolpy/esptool.py"])
        self.assertEqual(guard.esptool_command("", "/pkgs/tool-esptoolpy/esptool.py", py),
                         [py, "/pkgs/tool-esptoolpy/esptool.py"])
        self.assertEqual(guard.esptool_command("/venv/bin/esptool", "", py), ["/venv/bin/esptool"])
        self.assertEqual(guard.esptool_command("xtensa-esp32-elf-objcopy", "", py), [py, "-m", "esptool"])
        self.assertEqual(guard.esptool_command("", "", py), [py, "-m", "esptool"])

    def test_macs(self):
        self.assertEqual(guard.normalize_mac("A4-CF-12-9B-E5-F6"), "a4:cf:12:9b:e5:f6")
        self.assertEqual(guard.normalize_mac("a4cf129be5f6"), "a4:cf:12:9b:e5:f6")
        self.assertEqual(guard.normalize_mac("nope"), "")
        self.assertEqual(guard.allowed_macs("A4:CF:12:9B:E5:F6, junk,a4cf129be5f7"),
                         {"a4:cf:12:9b:e5:f6", "a4:cf:12:9b:e5:f7"})
        self.assertEqual(guard.allowed_macs(None), set())


class IdentifyTests(unittest.TestCase):
    MAC = "a4:cf:12:9b:e5:f6"

    def test_pumpkin_clock_with_name(self):
        namespaces, values = guard.parse_nvs(PUMPKIN_NVS)
        self.assertEqual(namespaces, {"sugarota", "tc001cfg"})
        self.assertEqual(values[("tc001cfg", "clk_name")], "Emilie")
        self.assertEqual(values[("tc001cfg", "brightness")], 40)
        self.assertEqual(guard.identify(PUMPKIN_NVS, self.MAC), ("pumpkin", 'Pumpkin clock "Emilie"'))

    def test_pumpkin_1_clock(self):
        data = nvs(nvs_page([
            (0, 0x01, "pumpkin", 4),
            (4, 0x21, "device", '{"name":"Klas 1B","zone":"Europe/Brussels","posix":"CET-1CEST,M3.5.0,M10.5.0/3","password":""}'),
        ]))
        self.assertEqual(guard.identify(data, self.MAC), ("pumpkin", 'Pumpkin clock "Klas 1B"'))
        unnamed = nvs(nvs_page([(0, 0x01, "pumpkin", 4), (4, 0x21, "display", "{}")]))
        self.assertEqual(guard.identify(unnamed, self.MAC), ("pumpkin", 'Pumpkin clock "pumpkin-e5f6"'))

    def test_pumpkin_clock_default_name(self):
        data = nvs(nvs_page([(0, 0x01, "tc001cfg", 3), (3, 0x21, "clk_name", "")]))
        self.assertEqual(guard.identify(data, self.MAC), ("pumpkin", 'Pumpkin clock "pumpkin-e5f6"'))

    def test_sugarclock_clock(self):
        data = nvs(nvs_page([(0, 0x01, "tc001cfg", 1), (1, 0x01, "brightness", 9)]))
        self.assertEqual(guard.identify(data, self.MAC)[0], "sugarclock")

    def test_other_devices_are_unknown(self):
        esphome = nvs(nvs_page([(0, 0x01, "esphome", 1), (1, 0x21, "wifi_ssid", "Home")]))
        self.assertEqual(guard.identify(esphome, self.MAC)[0], "unknown")
        self.assertEqual(guard.identify(b"\xff" * guard.NVS_SIZE, self.MAC)[0], "unknown")   # erased
        self.assertEqual(guard.identify(b"", self.MAC)[0], "unknown")
        self.assertEqual(guard.identify(None, self.MAC)[0], "unknown")
        self.assertEqual(guard.identify(bytes(range(256)) * 80, self.MAC)[0], "unknown")    # noise

    def test_settings_spread_over_pages_and_erased_entries(self):
        first = nvs_page([(0, 0x01, "tc001cfg", 1)], seq=1, state=0xFFFFFFFC)
        second = nvs_page([(1, 0x21, "clk_name", "Keuken")], seq=2)
        self.assertEqual(guard.identify(nvs(second, first), self.MAC), ("pumpkin", 'Pumpkin clock "Keuken"'))
        # An erased namespace entry (state 00) doesn't count.
        page = bytearray(nvs_page([(0, 0x01, "tc001cfg", 1), (1, 0x21, "clk_name", "X")]))
        page[32] &= 0b11111100
        self.assertEqual(guard.identify(nvs(bytes(page)), self.MAC)[0], "unknown")
        # A free (never used) page is ignored.
        self.assertEqual(guard.identify(nvs(nvs_page([(0, 0x01, "tc001cfg", 1)], state=0xFFFFFFFF)),
                                        self.MAC)[0], "unknown")


class DecideTests(unittest.TestCase):
    MAC = "a4:cf:12:9b:e5:f6"

    def test_upload_to_a_known_clock_goes(self):
        for kind in ("pumpkin", "sugarclock"):
            self.assertEqual(guard.decide(["upload"], kind, self.MAC, set(), 4), "go")
            self.assertEqual(guard.decide(["uploadfs"], kind, self.MAC, set(), 8), "go")

    def test_erase_always_asks(self):
        self.assertEqual(guard.decide(["erase"], "pumpkin", self.MAC, set(), 4), "ask")
        self.assertEqual(guard.decide(["erase", "upload"], "pumpkin", self.MAC, set(), 4), "ask")

    def test_unknown_devices_ask(self):
        self.assertEqual(guard.decide(["upload"], "unknown", self.MAC, set(), 4), "ask")
        self.assertEqual(guard.decide(["upload"], "unknown", "", set(), None), "ask")

    def test_confirmed_mac_goes(self):
        self.assertEqual(guard.decide(["erase"], "unknown", self.MAC, {self.MAC}, 4), "go")
        self.assertEqual(guard.decide(["erase"], "unknown", self.MAC, {"a4:cf:12:9b:e5:f7"}, 4), "ask")
        self.assertEqual(guard.decide(["erase"], "unknown", "", {""}, 4), "ask")

    def test_small_flash_stops_even_when_confirmed(self):
        self.assertTrue(guard.decide(["upload"], "pumpkin", self.MAC, {self.MAC}, 2).startswith("stop:"))


class FakeEnv:
    def __init__(self, upload_port=""):
        self.vars = {"UPLOAD_PORT": upload_port, "UPLOAD_SPEED": "115200",
                     "UPLOADER": "/pkgs/tool-esptoolpy/esptool.py", "OBJCOPY": "",
                     "PYTHONEXE": "/venv/bin/python"}

    def subst(self, text):
        return self.vars.get(text.lstrip("$"), "")

    def BoardConfig(self):
        return {"build.mcu": "esp32"}

    def Replace(self, **kwargs):
        self.vars.update(kwargs)


class GuardFlowTests(unittest.TestCase):
    """guard() end to end, with fake ports, a fake esptool and no terminal."""

    def run_guard(self, targets, ports, nvs_data=PUMPKIN_NVS, upload_port="", env_vars=None, answer=None):
        from unittest import mock
        env = FakeEnv(upload_port)
        calls = []

        def fake_esptool(base, port_name, baud, mcu, args):
            calls.append((port_name, args[0]))
            if args[0] == "read_flash":
                Path(args[-1]).write_bytes(nvs_data)
                return "Chip is ESP32-D0WD-V3\nMAC: a4:cf:12:9b:e5:f6\n"
            return EsptoolTests.V4

        stdin = mock.Mock()
        stdin.isatty.return_value = answer is not None
        stdin.readline.return_value = (answer or "") + "\n"
        with mock.patch("serial.tools.list_ports.comports", return_value=ports), \
             mock.patch.object(guard, "run_esptool", fake_esptool), \
             mock.patch.object(guard.sys, "stdin", stdin), \
             mock.patch.dict(guard.os.environ, env_vars or {}, clear=False), \
             mock.patch("builtins.print"):
            try:
                guard.guard(env, targets)
                return env.vars["UPLOAD_PORT"], calls, None
            except SystemExit as stop:
                return env.vars["UPLOAD_PORT"], calls, stop.code

    CLOCK = port("/dev/cu.usbserial-1410", location="20-1")
    OTHER = port("/dev/cu.usbserial-1420", location="20-2")
    CP2102 = port("/dev/cu.SLAB_USBtoUART", vid=0x10C4, pid=0xEA60)

    def test_build_only_touches_nothing(self):
        self.assertEqual(self.run_guard(["size"], [self.CLOCK]), ("", [], None))

    def test_upload_to_the_one_pumpkin_clock(self):
        chosen, calls, code = self.run_guard(["upload"], [self.CLOCK, self.CP2102])
        self.assertIsNone(code)
        self.assertEqual(chosen, "/dev/cu.usbserial-1410")
        self.assertEqual([c[1] for c in calls], ["flash_id", "read_flash"])   # read only

    def test_two_candidates_stop_before_touching_either(self):
        chosen, calls, code = self.run_guard(["upload"], [self.CLOCK, self.OTHER])
        self.assertEqual((chosen, calls, code), ("", [], 1))

    def test_no_clock_stops(self):
        self.assertEqual(self.run_guard(["erase"], [self.CP2102])[1:], ([], 1))

    def test_chosen_port_is_still_checked(self):
        chosen, calls, code = self.run_guard(["upload"], [self.CLOCK, self.OTHER],
                                             upload_port="/dev/cu.usbserial-1420")
        self.assertIsNone(code)
        self.assertEqual({c[0] for c in calls}, {"/dev/cu.usbserial-1420"})

    def test_erase_needs_a_yes(self):
        self.assertEqual(self.run_guard(["erase"], [self.CLOCK], answer="y")[2], None)
        self.assertEqual(self.run_guard(["erase"], [self.CLOCK], answer="")[2], 1)
        self.assertEqual(self.run_guard(["erase"], [self.CLOCK], answer="n")[2], 1)
        # No terminal: refused, unless the MAC was confirmed up front.
        self.assertEqual(self.run_guard(["erase"], [self.CLOCK])[2], 1)
        self.assertIsNone(self.run_guard(["erase"], [self.CLOCK],
                                         env_vars={"PUMPKIN_DEVICE": "A4:CF:12:9B:E5:F6"})[2])

    def test_unknown_device_needs_a_yes(self):
        blank = b"\xff" * guard.NVS_SIZE
        self.assertEqual(self.run_guard(["upload"], [self.CLOCK], nvs_data=blank)[2], 1)
        self.assertIsNone(self.run_guard(["upload"], [self.CLOCK], nvs_data=blank, answer="yes")[2])

    def test_skip_switch(self):
        self.assertEqual(self.run_guard(["erase"], [self.CLOCK, self.OTHER],
                                        env_vars={"PUMPKIN_SKIP_USB_GUARD": "1"}), ("", [], None))


if __name__ == "__main__":
    unittest.main()
