# Pumpkin firmware: installing, updating, recovery

## Prerequisites

### Hardware
- Ulanzi TC001 smart pixel clock
- USB-C data cable (the included cable can be flaky — use a quality one)
- Computer with a free USB port (connect directly, not through a hub)

### Software
- [PlatformIO CLI](https://platformio.org/install/cli) or PlatformIO IDE (VS Code extension)
- [Node.js](https://nodejs.org) 20 or newer, to build the dashboard
- CH340 USB-to-serial driver (see below)

### CH340 Driver Installation

The Ulanzi TC001 uses a CH340 USB-to-serial chip.

**macOS**: Usually works out of the box on 10.9+. If not detected, install the [CH340 driver](https://sparks.gogo.co.nz/ch340.html).

**Windows**: Download and run the [CH340 driver installer](https://sparks.gogo.co.nz/ch340.html). After installation, the device appears as `USB-SERIAL CH340 (COM##)` in Device Manager.

**Linux**: Built into the kernel on most distributions. No installation needed.

---

## Step 1: Backup Factory Firmware (Recommended)

Before flashing custom firmware, back up the stock firmware so you can restore it later.

```bash
# Install esptool if you don't have it
pip install esptool

# Find your serial port
# macOS:  /dev/cu.usbserial-*  or  /dev/cu.wchusbserial-*
# Linux:  /dev/ttyUSB0
# Windows: COM3, COM4, etc. (check Device Manager)

# Read full 8MB flash (takes ~2 minutes)
esptool.py -p /dev/cu.usbserial-1410 -b 921600 read_flash 0x0 0x800000 tc001_factory_backup.bin
```

Keep `tc001_factory_backup.bin` somewhere safe.

---

## Step 2: Build the Firmware

```bash
cd firmware
pip install -r requirements-build.txt   # PlatformIO, pinned
pio run
```

The first build downloads the ESP32 toolchain, installs the dashboard's npm
packages and takes a few minutes. The build ends with the image size (about
75% of an update slot) and its marker:

```
[post-build] 1,385,520 of 1,835,008 bytes (75.5% of an OTA slot)
[post-build] Image marker PUMPKIN-IMAGE|ulanzi-tc001-esp32-4mb|
```

---

## Step 3: Connect the Ulanzi TC001

1. Plug the USB-C cable into the Ulanzi TC001 and your computer
2. The device should power on and the serial port should appear

Verify the connection:
```bash
# macOS/Linux
ls /dev/cu.* /dev/ttyUSB* 2>/dev/null

# Windows (PowerShell)
Get-WMIObject Win32_SerialPort | Select-Object DeviceID, Description
```

### Entering Flash Mode (if needed)

If the upload fails with a connection error:
1. Unplug the USB cable
2. Hold the **middle button** on the Ulanzi TC001
3. Plug in the USB cable while holding the button
4. Release after 1-2 seconds
5. Retry the upload

---

## Step 4: Flash the Firmware

### Option A: PlatformIO (Recommended)

Every target that writes to a device (`upload`, `erase`) goes
through the USB guard first (`scripts/upload_guard.py`). It picks the one CH340
device that is connected, reads its chip, MAC, flash size and settings
(read only), and only then lets PlatformIO write, to that port and no other:

- a clock that already runs Pumpkin or SugarClock: uploads without asking;
- anything else, and every erase: asks `[y/N]` and shows the MAC first;
- no CH340 device, or more than one: stops. Choose with `--upload-port`.

Without a terminal to ask in (scripts, CI) it refuses, unless the device's MAC
is confirmed up front: `PUMPKIN_DEVICE=a4:cf:12:9b:e5:f6 pio run -t upload`
(comma-separated for more than one). `PUMPKIN_SKIP_USB_GUARD=1` turns it off.

```bash
pio run --target upload    # the dashboard is built into the firmware
```

To choose the port yourself (the guard still checks what's on it):
```bash
pio run --target upload --upload-port /dev/cu.usbserial-1410
```

### Coming from Pumpkin 0.x or SugarClock (keep settings)

Upload 1.0 the usual way: through the old web page (Device → Update firmware)
or over USB, **without erasing**. On its first start 1.0 carries over:

- Wi-Fi (also school logins and the saved certificate);
- the bridge address and clock token, or demo data;
- the clock's name and dashboard password;
- screens, rotation, glucose unit and ranges, 24-hour time, date, pet, brightness;
- night mode, as the same awake hours on every day.

Then it removes the old settings, which held the passwords a second time. Not
carried over: Dexcom, LibreLinkUp and Nightscout logins, timers, buzzer alerts,
colours (1.0 has three colour sets) and the time zone. **Check the time zone on
the Device tab** if you're not in Belgium: the old firmware only kept the raw
rules, and 1.0 starts on Europe/Brussels.

Going back to 0.12 afterwards means setting the clock up again.

---

## Step 5: Verify the Flash

```bash
pio device monitor
```

```
[boot] Pumpkin 1.0.0, reset: power_on
[time] RTC found, time loaded
[ota] 1.0.0, image PUMPKIN-IMAGE|ulanzi-tc001-esp32-4mb|
[wifi] No network saved: setup hotspot
[wifi] Setup hotspot Pumpkin-Setup-E5F6 on 192.168.4.1
[boot] Ready, 190000 bytes free
```

The matrix shows a pumpkin, then scrolls the setup network's name.

---

## Step 6: Set it up

### Wi-Fi

A clock without Wi-Fi opens an open network named `Pumpkin-Setup-XXXX`
(`XXXX` is unique per clock).

1. Join it from a phone or laptop.
2. If a sign-in window pops up, it only points you on: open Safari or Chrome
   and go to `http://192.168.4.1`. (That window closes too soon to join a
   school network.)
3. Pick the network, choose **Password** or **School or work login**, and join.
   The clock tries it first and keeps it only when it works; the page and the
   matrix say what went wrong otherwise (wrong password, network not found,
   no answer).
4. Once it's on, the clock scrolls its address, e.g. `pumpkin-e5f6.local`
   and its IP address. Open that on your normal network.

When the saved network stops working, the setup hotspot comes back after two
minutes; the clock keeps retrying the saved network while nobody is on it.

### The dashboard

| Tab | What's there |
|---|---|
| **Display** | live mirror of the matrix, screens and their order, glucose unit, ranges and colours, time and date, pump alerts (and **Clear remembered alerts** on the bridge), brightness |
| **Pet** | which pet (previews drawn by the clock), what shows next to it, seasonal surprises, trying its poses on the clock |
| **Schedule** | awake hours per day; at night the clock dims or goes dark |
| **Connections** | pump data (bridge address and clock token, or demo data), Wi-Fi, the reachability checks |
| **Device** | name (its address becomes `name.local`), time zone, password, firmware updates, restart, factory reset, diagnostics |

For the bridge, see [`../docs/INSTALL.md`](../docs/INSTALL.md), step *Point the
clock at the bridge*.

### School and WPA2-Enterprise Wi-Fi

PEAP and EAP-TTLS with a username and password work; certificate logins
(EAP-TLS) don't. Easiest: flash at home without Wi-Fi, then set it up at
school from a phone on the setup hotspot. PEAP is right for most schools.

Enter the user name exactly as IT gives it (`user@school.org`,
`SCHOOL\user` or `user`); a right password with the wrong user name format is
still refused. The anonymous identity is optional, under **Advanced**.

By default the clock trusts the login server, like tapping "Trust" on a phone.
To check it instead, add the school's CA certificate (PEM) under **Advanced**.

What school IT needs to allow (plus normal DNS):

```text
TCP 443: the Pumpkin bridge (<name>.<account>.workers.dev, or its own domain)
UDP 123: pool.ntp.org, time.google.com, time.cloudflare.com
```

After joining, **Connections** shows whether names resolve (DNS), the bridge
answers (HTTPS) and time sync works (NTP) separately. Without NTP the clock
takes the time from the bridge's answers, to a second or two.

Hotel-style sign-in pages don't work: the clock has no browser. Use a travel
router or a phone hotspot there.

---

## Buttons

| Button | Press | Hold (1 s) |
|---|---|---|
| Left | previous screen | |
| Middle | the pet waves (on the pet screen) | show the clock's address |
| Right | next screen | |

## What the matrix says

| Shown | Meaning |
|---|---|
| glucose in green / amber / red | in range / low or high / urgent |
| glucose with a `!` | the reading is getting old |
| glucose in grey | the reading is too old (after the time set on Display) |
| `NO` / `WIFI`, red | the saved network isn't reachable |
| `NO` / `DATA`, red | the bridge doesn't answer or refuses the token |
| scrolling teal text | setup: which network to join, or what to open |
| scrolling amber text | on Wi-Fi, but the network blocks DNS or the bridge |

---

## Updating

**From the dashboard:** build with `pio run`, then **Device → Firmware**, pick
`.pio/build/esp32dev/firmware.bin` and install. The page names the clock and
refuses files that aren't Pumpkin firmware for this hardware. The clock
restarts and checks itself for about 15 seconds; if it isn't healthy, it goes
back to the previous version by itself and the Device tab says so. Details:
[`docs/ota-boot-validation.md`](docs/ota-boot-validation.md).

**Over USB:** `pio run -t upload`. Settings stay either way.

## Recovery

Updates never touch the bootloader or partition table. If the clock doesn't
start, enter flash mode (hold the middle button while connecting USB) and
`pio run -t upload` again; settings stay unless you erase.

**Forgot the password?** Only a USB erase clears it, with all other settings:
`pio run -t erase`, then `pio run -t upload`, and set the clock up again.
**Device → Factory reset** does the same from the dashboard.

## Restoring the Ulanzi firmware

With the backup from step 1:

```bash
esptool.py -p /dev/cu.usbserial-1410 -b 460800 write_flash 0x0 tc001_factory_backup.bin
```
