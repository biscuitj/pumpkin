# Pumpkin

A glucose display for a child on a Medtronic 780G pump: a pixel clock in the
room that shows the current value, trend, time in range, active insulin and
pump alerts (battery, reservoir, set and sensor change, a stopped pump).

- **`bridge/`**: Cloudflare Worker. The only CareLink client (care-partner
  account). Serves the clock one JSON document and uploads the same data to
  Nightscout.
- **`firmware/`**: firmware for the Ulanzi TC001 (ESP32, 32×8 LEDs). Based on
  [SugarClock](https://github.com/cdemeke/SugarClock), pulled in with
  `git subtree`.
- **`docs/`**: installation (`INSTALL.md`), data structures (`DATA-SCHEMA.md`),
  display spec (`CLOCK-DISPLAY-SPEC.md`).
- **`CHANGELOG.md`**: one version for bridge and firmware together.

## Working on it

```bash
# Bridge
cd bridge && npm install && npm test && npx wrangler deploy

# Firmware (PlatformIO)
cd firmware && pio run -t upload
python3 -m pytest -q tests/test_bridge_screens.py tests/test_clock_identity.py
```

Upstream SugarClock releases (fetch only, never pushed to):

```bash
git subtree pull --prefix=firmware upstream main --squash
```

## Licenses and disclaimer

- `firmware/` is based on SugarClock by Chris Demeke (MIT); that notice applies
  to the firmware and our changes to it.
- `bridge/src/alert-texts.js` is ported from xDrip+ (`TextMap.java`, GPL-3.0),
  so the bridge is GPL-3.0 when it's shared.
- Not affiliated with or endorsed by Medtronic. Uses an unofficial CareLink
  API that can change at any time. Not a medical device: never base treatment
  decisions on this display alone.
