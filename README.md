<p align="center">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="docs/assets/pumpkin-logo-dark.svg">
    <img src="docs/assets/pumpkin-logo.svg" alt="Pumpkin, the pump buddy for you and your kin" width="420">
  </picture>
</p>

# Pumpkin

A glucose display for a child on a Medtronic 780G pump: a pixel clock in the
room that shows the current value, trend, time in range, active insulin and
pump alerts (battery, reservoir, set and sensor change, a stopped pump).

- **`bridge/`**: Cloudflare Worker. The only CareLink client (care-partner
  account). Serves the clock one JSON document and, optionally, uploads the same
  data to Nightscout.
- **`firmware/`**: firmware for the Ulanzi TC001 (ESP32, 32×8 LEDs). Based on
  [SugarClock](https://github.com/cdemeke/SugarClock) 0.2.15 and branched off
  from there: no upstream updates are merged.
- **`docs/`**: installation (`INSTALL.md`, start here), data structures (`DATA-SCHEMA.md`),
  display spec (`CLOCK-DISPLAY-SPEC.md`).
- **`CHANGELOG.md`**: one version for bridge and firmware together.

**Installing:** [`docs/INSTALL.md`](docs/INSTALL.md).

## Working on it

```bash
# Bridge
cd bridge && npm install && npm test && npx wrangler deploy

# Firmware (PlatformIO)
cd firmware && pio run -t upload            # first time / recovery: over USB
                                            # (USB guard checks the device first)
python3 -m pytest -q tests/                 # host tests
# later updates: upload firmware/.pio/build/esp32dev/firmware.bin in the
# clock's web page (Device → Update firmware)
```

## Licenses and disclaimer

- `firmware/` is based on SugarClock by Chris Demeke (MIT); see
  `firmware/LICENSE`.
- `bridge/src/alert-texts.js` is ported from xDrip+ (`TextMap.java`, GPL-3.0),
  so the bridge is GPL-3.0 when it's shared.
- Not affiliated with or endorsed by Medtronic. Uses an unofficial CareLink
  API that can change at any time. Not a medical device: never base treatment
  decisions on this display alone.

## Support

Pumpkin is a personal project, made for one family and shared with others. If
it helps yours, you can [buy me a coffee](https://buymeacoffee.com/biscuitj_pumpkin).
It's a thank-you, not a purchase: it doesn't buy support, updates or any
guarantee, and everything above still applies.

Pumpkin's clock firmware comes from [SugarClock](https://github.com/cdemeke/SugarClock)
by Chris Demeke, who shared it for free. If you want to thank someone, he
deserves it too: a star on his repository is a good start.
