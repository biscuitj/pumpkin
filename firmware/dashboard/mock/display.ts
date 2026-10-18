/**
 * Mock display: settings (PATCH semantics and validation as in the contract) and a
 * rough renderer so the dashboard's mirror has something real to show. Pets come
 * from the old firmware's pixel data (companions.js next to this file). For the
 * real rendering, run the dashboard against the firmware simulator (firmware/host).
 */
import { existsSync, readFileSync } from 'node:fs';
import type { IncomingMessage, ServerResponse } from 'node:http';
import { fileURLToPath } from 'node:url';
import vm from 'node:vm';
import {
  COLOR_PRESETS,
  DISPLAY_LIMITS,
  PETS,
  type DisplayPatch,
  type DisplayPreview,
  type DisplaySettings,
  type GlucoseBand,
  type PetId,
  type PetSprites,
  type ScreenId,
} from '../src/contract.ts';
import { FONT, ARROWS, TRIANGLE } from './font.ts';
import { Invalid, readJson, send } from './http.ts';

const now = () => Date.now();
const BOOT = now();

export const settings: DisplaySettings = {
  screens: { tir: true, iob: true, alerts: true, time: true, pet: true },
  rotation: { auto: true, seconds: 8 },
  glucose: {
    unit: 'mgdl',
    urgent_low: 55,
    low: 70,
    high: 180,
    urgent_high: 250,
    stale_after_min: 20,
    low_lock: true,
    delta_flash: true,
    colors: 'classic',
  },
  time: { hour24: true, date: 'day_month' },
  pet: { id: 'patch', style: 'text', show_tir: false, seasonal: true },
  alerts: { replace_pet: false },
  brightness: { auto: true, level: 40 },
};

// ---------------------------------------------------------------- routes

export async function handleDisplay(key: string, req: IncomingMessage, res: ServerResponse) {
  switch (key) {
    case 'GET /api/display/settings':
      return send(res, 200, settings);

    case 'PATCH /api/display/settings': {
      const patch = (await readJson(req)) as DisplayPatch;
      const next = applyPatch(settings, patch);
      validate(next);
      Object.assign(settings, next);
      return send(res, 200, settings);
    }

    case 'GET /api/display/frame': {
      const { rgb, screen } = render();
      const etag = `"${hash(rgb)}"`;
      const headers = { ETag: etag, 'X-Screen': screen, 'Cache-Control': 'no-store' };
      if (req.headers['if-none-match'] === etag) return send(res, 304, undefined, headers);
      return send(res, 200, rgb, headers);
    }

    case 'GET /api/display/pets':
      return send(res, 200, petSprites());

    case 'POST /api/display/next':
      step(1);
      return send(res, 204);

    case 'POST /api/display/prev':
      step(-1);
      return send(res, 204);

    case 'POST /api/display/preview': {
      const p = (await readJson(req)) as DisplayPreview;
      if (p.screen === 'pet' && !PETS.some((x) => x.id === p.pet)) throw new Invalid('Unknown pet.', 'pet');
      preview = { body: p, until: now() + 8000 };
      return send(res, 202);
    }

    default:
      return send(res, 404, { error: `No such endpoint: ${key}` });
  }
}

function applyPatch(s: DisplaySettings, patch: DisplayPatch): DisplaySettings {
  const next = structuredClone(s) as unknown as Record<string, Record<string, unknown>>;
  for (const [group, values] of Object.entries(patch ?? {})) {
    if (!(group in next) || typeof values !== 'object' || values === null)
      throw new Invalid(`Unknown group ${group}.`, group);
    for (const [k, v] of Object.entries(values)) {
      if (!(k in next[group])) throw new Invalid(`Unknown setting ${group}.${k}.`, `${group}.${k}`);
      if (typeof v !== typeof next[group][k]) throw new Invalid(`${group}.${k} has the wrong type.`, `${group}.${k}`);
      next[group][k] = v;
    }
  }
  return next as unknown as DisplaySettings;
}

function validate(s: DisplaySettings) {
  const g = s.glucose;
  const { min, max } = DISPLAY_LIMITS.glucose_mgdl;
  for (const k of ['urgent_low', 'low', 'high', 'urgent_high'] as const) {
    if (!Number.isInteger(g[k]) || g[k] < min || g[k] > max)
      throw new Invalid(`Use a value from ${min} to ${max} mg/dL.`, `glucose.${k}`);
  }
  if (!(g.urgent_low < g.low)) throw new Invalid('Urgent low has to be below low.', 'glucose.urgent_low');
  if (!(g.low < g.high)) throw new Invalid('Low has to be below high.', 'glucose.low');
  if (!(g.high < g.urgent_high)) throw new Invalid('Urgent high has to be above high.', 'glucose.urgent_high');
  const st = DISPLAY_LIMITS.stale_after_min;
  if (g.stale_after_min < st.min || g.stale_after_min > st.max)
    throw new Invalid(`Use ${st.min} to ${st.max} minutes.`, 'glucose.stale_after_min');
  const r = DISPLAY_LIMITS.rotation_seconds;
  if (s.rotation.seconds < r.min || s.rotation.seconds > r.max)
    throw new Invalid(`Use ${r.min} to ${r.max} seconds.`, 'rotation.seconds');
  const b = DISPLAY_LIMITS.brightness;
  if (s.brightness.level < b.min || s.brightness.level > b.max)
    throw new Invalid(`Use ${b.min} to ${b.max}.`, 'brightness.level');
  if (!PETS.some((p) => p.id === s.pet.id)) throw new Invalid('Unknown pet.', 'pet.id');
  if (!(g.colors in COLOR_PRESETS)) throw new Invalid('Unknown colour preset.', 'glucose.colors');
}

// ---------------------------------------------------------------- rotation

let manual: { screen: ScreenId; at: number } | null = null;
let preview: { body: DisplayPreview; until: number } | null = null;

function rotation(): ScreenId[] {
  const on = settings.screens;
  const list: ScreenId[] = ['glucose'];
  if (on.tir) list.push('tir');
  if (on.iob) list.push('iob');
  if (on.time) list.push('time');
  // The mock always has one pump alert, so the alerts screen is in the rotation.
  if (on.pet && on.alerts && settings.alerts.replace_pet) list.push('alerts');
  else {
    if (on.pet) list.push('pet');
    if (on.alerts) list.push('alerts');
  }
  return list;
}

function lowLocked(): boolean {
  return settings.glucose.low_lock && glucoseNow() < settings.glucose.low;
}

function currentScreen(): ScreenId {
  if (lowLocked()) return 'glucose';
  const list = rotation();
  const ms = settings.rotation.seconds * 1000;
  if (manual && list.includes(manual.screen)) {
    if (!settings.rotation.auto) return manual.screen;
    const steps = Math.floor((now() - manual.at) / ms);
    return list[(list.indexOf(manual.screen) + steps) % list.length];
  }
  if (!settings.rotation.auto) return list[0];
  return list[Math.floor((now() - BOOT) / ms) % list.length];
}

function step(dir: 1 | -1) {
  if (lowLocked()) return;
  const list = rotation();
  const i = list.indexOf(currentScreen());
  manual = { screen: list[(i + dir + list.length) % list.length], at: now() };
  preview = null;
}

// ---------------------------------------------------------------- data

/** Slow wander through the whole range, so every colour shows up within a few minutes. */
function glucoseNow(): number {
  const t = (now() - BOOT) / 1000;
  return Math.round(150 + 110 * Math.sin(t / 40));
}

function band(mgdl: number): GlucoseBand {
  const g = settings.glucose;
  if (mgdl < g.urgent_low) return 'urgent_low';
  if (mgdl < g.low) return 'low';
  if (mgdl > g.urgent_high) return 'urgent_high';
  if (mgdl > g.high) return 'high';
  return 'in_range';
}

function trend(): keyof typeof ARROWS {
  const slope = Math.cos((now() - BOOT) / 40_000);
  if (slope > 0.6) return 'up';
  if (slope > 0.2) return 'upright';
  if (slope > -0.2) return 'flat';
  return slope > -0.6 ? 'downright' : 'down';
}

// ---------------------------------------------------------------- renderer

type RGB = [number, number, number];
const rgbOf = (hex: string): RGB => [1, 3, 5].map((i) => parseInt(hex.slice(i, i + 2), 16)) as RGB;
const colors = () => COLOR_PRESETS[settings.glucose.colors].colors;

class Matrix {
  px = new Uint8Array(768);
  set(x: number, y: number, c: RGB) {
    if (x < 0 || x > 31 || y < 0 || y > 7) return;
    this.px.set(c, (y * 32 + x) * 3);
  }
  glyph(rows: string[], x: number, y: number, c: RGB) {
    rows.forEach((row, dy) => [...row].forEach((b, dx) => b === '1' && this.set(x + dx, y + dy, c)));
    return rows[0].length;
  }
  width(text: string) {
    return [...text].reduce((w, ch) => w + (FONT[ch]?.[0].length ?? 3) + 1, -1);
  }
  text(text: string, x: number, y: number, c: RGB) {
    for (const ch of text) x += this.glyph(FONT[ch] ?? FONT[' '], x, y, c) + 1;
    return x;
  }
  centered(text: string, c: RGB, extra = 0) {
    return this.text(text, Math.floor((32 - this.width(text) - extra) / 2), 0, c);
  }
}

function render(): { rgb: Uint8Array; screen: string } {
  const m = new Matrix();
  if (preview && preview.until < now()) preview = null;
  const p = preview?.body;
  const mgdl = p?.screen === 'glucose' ? p.mgdl : glucoseNow();
  const urgentLow = mgdl < settings.glucose.urgent_low;

  const screen: ScreenId = p && !urgentLow ? p.screen : currentScreen();
  switch (screen) {
    case 'glucose': {
      const c = rgbOf(colors()[band(mgdl)]);
      const value = settings.glucose.unit === 'mmol' ? (mgdl / 18.016).toFixed(1) : String(mgdl);
      const x = m.centered(value, c, 6);
      m.glyph(ARROWS[trend()], x + 1, 0, c);
      break;
    }
    case 'tir':
      m.centered('82%', rgbOf('#34a853'));
      break;
    case 'iob':
      m.centered('0.35U', rgbOf('#00bcd4'));
      break;
    case 'time': {
      const d = new Date();
      let h = d.getHours();
      if (!settings.time.hour24) h = h % 12 || 12;
      const colon = Math.floor(now() / 1000) % 2 === 0 ? ':' : ' ';
      const hh = settings.time.hour24 ? String(h).padStart(2, '0') : String(h);
      m.centered(`${hh}${colon}${String(d.getMinutes()).padStart(2, '0')}`, [255, 255, 255]);
      break;
    }
    case 'alerts':
      m.glyph(TRIANGLE, 12, 0, rgbOf('#fbbc04'));
      break;
    case 'pet': {
      const range = p?.screen === 'pet' ? p.range : band(mgdl);
      drawPet(m, p?.screen === 'pet' ? p.pet : settings.pet.id, range);
      break;
    }
  }
  return { rgb: m.px, screen };
}

// ---------------------------------------------------------------- pets

const PET_INDEX: Record<PetId, number> = {
  patch: 7,
  toffee: 8,
  kiko: 9,
  domino: 10,
  hoot: 11,
  leo: 12,
  stella: 13,
  kobi: 14,
};

type Companions = {
  palette: Record<string, string>;
  frame: (id: number, ms: number, sleepy: boolean, happy: boolean, style: number, range: number) => string[][];
};

const companions: Companions | null = (() => {
  try {
    const file = fileURLToPath(new URL('./companions.js', import.meta.url));
    if (!existsSync(file)) return null;
    const scope: { PixelCompanions?: Companions } = {};
    vm.runInNewContext(readFileSync(file, 'utf8'), { globalThis: scope });
    return scope.PixelCompanions ?? null;
  } catch {
    return null;
  }
})();

function drawPet(m: Matrix, pet: PetId, range: GlucoseBand) {
  const r = range === 'low' || range === 'urgent_low' ? 1 : range === 'high' || range === 'urgent_high' ? 2 : 0;
  if (!companions) {
    m.centered(PETS.find((p) => p.id === pet)!.name.slice(0, 5).toUpperCase(), [255, 157, 72]);
    return;
  }
  const style = { text: 0, icon: 1, pet_only: 2 }[settings.pet.style];
  const rows = companions.frame(PET_INDEX[pet], now() - BOOT, false, false, style, r);
  const preset = colors();
  const status: Record<string, string> = { a: preset.low, i: preset.in_range, r: preset.high };
  rows.forEach((row, y) =>
    row.forEach((key, x) => {
      if (key !== '.') m.set(x, y, rgbOf(status[key] ?? companions.palette[key] ?? '#000000'));
    }),
  );
}

// Same shape as the clock's answer: the in-range pose, two frames, cropped.
function petSprites(): PetSprites {
  const colors: Record<string, string> = {};
  const pets = PETS.map((p) => {
    if (!companions) return { id: p.id, frames: [Array(8).fill('.'), Array(8).fill('.')] };
    const raw = [0, 650].map((ms) => companions.frame(PET_INDEX[p.id], ms, false, false, 2, 0));
    let left = 32;
    let right = -1;
    for (const rows of raw) {
      for (const row of rows) {
        row.forEach((k, x) => {
          if (k === '.') return;
          left = Math.min(left, x);
          right = Math.max(right, x);
          colors[k] = companions.palette[k] ?? '#000000';
        });
      }
    }
    return { id: p.id, frames: raw.map((f) => f.map((row) => row.slice(left, right + 1).join(''))) };
  });
  return { frame_ms: 650, colors, pets };
}

function hash(b: Uint8Array): string {
  let h = 2166136261;
  for (const v of b) h = Math.imul(h ^ v, 16777619);
  return (h >>> 0).toString(16);
}
