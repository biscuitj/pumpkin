# Pumpkin firmware

Firmware for the Ulanzi TC001 pixel clock (ESP32, 32×8 LEDs). It shows glucose,
trend, time in range, active insulin and pump alerts from the Pumpkin bridge
(`../bridge`), and still reads Dexcom Share, LibreLinkUp or Nightscout directly.

Based on [SugarClock](https://github.com/cdemeke/SugarClock) 0.2.15
(commit `e68c4f1`) by Chris Demeke, MIT licensed. Pumpkin branched off there and
doesn't take upstream updates; see `LICENSE` and the project `CHANGELOG.md`.

What's different from SugarClock:
- **Bridge screens:** pump alerts (icons), sport mode, active insulin, clear
  pump alerts from the web page.
- **Patch the pumpkin:** a pumpkin display pet, the default on a new clock.
- **Seven more pets:** Toffee (cat), Kiko (frog), Domino (penguin), Hoot (owl),
  Leo (lion), Stella (unicorn) and Kobi (crab).
- **Leaner:** no weather screen, push notifications or system monitor.
- **Night mode Full Mute:** black screen and a silent buzzer during night hours.
- **Clocks on one network:** clock names, `name.local` addresses, a clock list
  and copying settings between clocks.
- **Updates:** upload a `firmware.bin` in the clock's web page (Device →
  Update firmware), with automatic rollback. No update server, no check-in
  with any server.
- **Optional password** for the web page and API (user `pumpkin`).

Installing, updating and recovery: [`INSTALL.md`](INSTALL.md).

## Development

```bash
pip install -r requirements-build.txt      # PlatformIO, pinned
pio run                                     # build
pio run -t upload                           # flash over USB
python3 -m pytest -q tests/                 # host tests (needs a C++ compiler)
python3 scripts/check_layout.py             # partitions, version, rollback hook (after pio run)
```

The version comes from `VERSION` (strict `major.minor.patch`) and is injected as
`PUMPKIN_VERSION` at build time.

## Acknowledgments

- [SugarClock](https://github.com/cdemeke/SugarClock) by Chris Demeke, the base of this firmware
- [AWTRIX3](https://blueforcer.github.io/awtrix3/#/), LED matrix firmware for the TC001 that inspired SugarClock
- [pydexcom](https://github.com/gagebenne/pydexcom), Dexcom Share API reference
- [nightscout-librelink-up](https://github.com/timoschlueter/nightscout-librelink-up), LibreLinkUp API reference
