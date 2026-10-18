# Pumpkin dashboard

The clock's web UI: Preact + Signals, built with Vite into **one self-contained
`dist/index.html`** (JS and CSS inlined, no external requests) so it works in
captive-portal mode and ships inside the firmware image.

```sh
npm install
npm run dev      # http://localhost:5173 against a mock clock
npm run build    # typecheck + dist/index.html and dist/setup.html
```

## Contract first

`src/contract.ts` is the HTTP API the firmware has to implement: request and
response shapes, error format, which calls are async (202 + poll). The dashboard
is built against it, and `mock/clock.ts` implements it in the dev server, so
screens can be built before the firmware exists. The mock's header lists inputs
that trigger failure paths (wrong password, CareLink login needed, …).
`mock/display.ts` renders rough mirror frames, with the pets drawn from
`mock/companions.js` (pixel data from the old firmware).

To develop against a real clock instead, copy `.env.example` to `.env.local` and
set `PUMPKIN_HOST`; `/api/*` is then proxied to it. The firmware simulator works
the same way and runs the clock's real API, engine and rendering on your computer:

```sh
make -C ../host sim && ../host/sim          # http://localhost:8080
PUMPKIN_HOST=localhost:8080 npm run dev
```

## Layout

- `src/tabs/<tab>/`: one folder per tab; `describe.ts` holds the plain-language
  status texts, kept out of the components so they can be tested.
- `src/ui/`: small shared pieces (fields, notices, LEDs, icons, the logo from `docs/assets`).
- `src/lib/poll.ts`: polling that pauses when the page is hidden, to spare the ESP32.

The single-file output is enforced by a small plugin in `vite.config.ts`: the
build fails if anything (an image, a lazy chunk) can't be inlined. Use inline
SVG for icons.

`dist/setup.html` is the page the clock serves over its setup hotspot. The
firmware build (`scripts/embed_dashboard.py`, run by `pio run`) rebuilds the
dashboard when its sources changed and embeds both pages, gzipped.
