/**
 * Mock device: name, password, time zone, diagnostics, firmware update, restart,
 * factory reset and "clear remembered alerts". Restarts make every /api call fail
 * for a few seconds, like a real clock going away.
 *
 * Update files: anything starting with 0xE9 (ESP32) whose chip id is 0 and which
 * contains IMAGE_MARKER installs. Add the text ROLLBACK to simulate a failed first
 * boot. Other files are refused with the firmware's error codes.
 */
import type { IncomingMessage, ServerResponse } from 'node:http';
import {
  IMAGE_MARKER,
  type AlertsClear,
  type DeviceInfo,
  type DevicePatch,
  type UpdateError,
  type UpdateStatus,
} from '../src/contract.ts';
import { Invalid, readJson, send } from './http.ts';

const now = () => Date.now();
const BOOT = now();
let bootedAt = BOOT;
let offlineUntil = 0;

const device = {
  name: 'Keuken',
  password: '',
  version: '1.0.0-dev',
  zone: 'Europe/Brussels',
  rolledBack: null as DeviceInfo['firmware']['rolled_back'],
  resetReason: 'power_on' as DeviceInfo['diagnostics']['reset_reason'],
};

let update: UpdateStatus = { state: 'idle', received: 0, error: null };
let clear: AlertsClear & { at?: number } = { state: 'idle', error: null };

export const firmwareVersion = () => device.version;

/** True while the mock clock is "restarting": the router drops those requests. */
export const isOffline = () => now() < offlineUntil;

function restart(ms = 4000, after?: () => void) {
  setTimeout(() => {
    offlineUntil = now() + ms;
    bootedAt = offlineUntil;
    device.resetReason = 'software';
    setTimeout(() => after?.(), ms);
  }, 300);
}

export const slug = (name: string) =>
  name
    .normalize('NFD')
    .replace(/[̀-ͯ]/g, '')
    .toLowerCase()
    .replace(/[^a-z0-9]+/g, '-')
    .replace(/^-|-$/g, '')
    .slice(0, 24) || 'pumpkin';

function info(): DeviceInfo {
  const d = new Date();
  const pad = (n: number) => String(n).padStart(2, '0');
  const t = (now() - BOOT) / 1000;
  return {
    name: device.name,
    local_name: `${slug(device.name)}.local`,
    password_set: device.password !== '',
    firmware: { version: device.version, hardware: 'Ulanzi TC001 (mock)', rolled_back: device.rolledBack },
    time: {
      zone: device.zone,
      synced: true,
      source: 'ntp',
      local: `${d.getFullYear()}-${pad(d.getMonth() + 1)}-${pad(d.getDate())}T${pad(d.getHours())}:${pad(d.getMinutes())}`,
    },
    diagnostics: {
      uptime_sec: Math.round((now() - bootedAt) / 1000),
      reset_reason: device.resetReason,
      free_heap: 142_000 + Math.round(Math.sin(t / 7) * 6000),
      min_free_heap: 118_400,
      largest_free_block: 81_908,
      light: Math.round(1800 + 900 * Math.sin(t / 20)),
      brightness_now: 42,
      // Pretend the middle button is held for a second every 6 s, so the test shows something.
      buttons: { left: false, middle: Math.floor(t) % 6 === 0, right: false },
      last_fetch_error: null,
    },
  };
}

export async function handleDevice(key: string, req: IncomingMessage, res: ServerResponse) {
  switch (key) {
    case 'GET /api/device':
      return send(res, 200, info());

    case 'PATCH /api/device': {
      const p = (await readJson(req)) as DevicePatch;
      if (p.name !== undefined) {
        const name = p.name.trim();
        if (!name) throw new Invalid('Enter a name.', 'name');
        if (name.length > 32) throw new Invalid('Names are 32 characters at most.', 'name');
        device.name = name;
      }
      if (p.time_zone) {
        if (!p.time_zone.zone || !p.time_zone.posix) throw new Invalid('Pick a time zone.', 'time_zone');
        device.zone = p.time_zone.zone;
      }
      return send(res, 200, info());
    }

    case 'PUT /api/device/password': {
      const { password } = (await readJson(req)) as { password: string };
      if (typeof password !== 'string') throw new Invalid('Missing password.', 'password');
      if (password && (password.length < 4 || password.length > 64))
        throw new Invalid('Use 4 to 64 characters.', 'password');
      device.password = password;
      return send(res, 204);
    }

    case 'POST /api/device/restart':
      restart();
      return send(res, 202);

    case 'POST /api/device/factory-reset': {
      const { confirm } = (await readJson(req)) as { confirm?: string };
      if (confirm !== 'erase') throw new Invalid('Send { "confirm": "erase" }.', 'confirm');
      restart(5000, () => {
        device.name = 'Pumpkin';
        device.password = '';
      });
      return send(res, 202);
    }

    case 'GET /api/update':
      return send(res, 200, update);

    case 'POST /api/update': {
      if (update.state === 'receiving') return send(res, 409, { error: 'An update is already running.', field: 'busy' });
      update = { state: 'receiving', received: 0, error: null };
      const body = await readBody(req, (n) => (update.received = n));
      const verdict = check(body);
      if (verdict) {
        update = { state: 'failed', received: body.length, error: verdict };
        return send(res, 400, { error: MESSAGES[verdict], field: verdict });
      }
      update = { state: 'restarting', received: body.length, error: null };
      const rollback = body.includes(Buffer.from('ROLLBACK'));
      const from = device.version;
      restart(6000, () => {
        if (rollback) device.rolledBack = { from_version: bump(from), reason: 'The new firmware did not start properly.' };
        else device.version = bump(from);
        update = { state: 'idle', received: 0, error: null };
      });
      return send(res, 202);
    }

    case 'POST /api/alerts/clear':
      clear = { state: 'running', error: null, at: now() };
      return send(res, 202);

    case 'GET /api/alerts/clear': {
      if (clear.state === 'running' && clear.at && now() - clear.at > 1500) clear = { state: 'done', error: null };
      return send(res, 200, { state: clear.state, error: clear.error } satisfies AlertsClear);
    }

    default:
      return send(res, 404, { error: `No such endpoint: ${key}` });
  }
}

const MESSAGES: Partial<Record<UpdateError, string>> = {
  not_firmware: "That file isn't clock firmware.",
  wrong_chip: 'That firmware is for a different chip.',
  not_pumpkin: "That's ESP32 firmware, but not Pumpkin.",
  wrong_hardware: 'That Pumpkin firmware is for different hardware.',
  truncated: 'The file is incomplete.',
};

function check(b: Buffer): UpdateError | null {
  if (b.length < 24) return 'truncated';
  if (b[0] !== 0xe9) return 'not_firmware';
  if (b.readUInt16LE(12) !== 0) return 'wrong_chip';
  if (b.includes(Buffer.from(IMAGE_MARKER))) return null;
  return b.includes(Buffer.from('PUMPKIN-IMAGE|')) ? 'wrong_hardware' : 'not_pumpkin';
}

const bump = (v: string) => v.replace(/^(\d+)\.(\d+)\.(\d+)/, (_, a, b, c) => `${a}.${b}.${Number(c) + 1}`);

function readBody(req: IncomingMessage, progress: (n: number) => void): Promise<Buffer> {
  return new Promise((resolve, reject) => {
    const chunks: Buffer[] = [];
    let n = 0;
    req.on('data', (c: Buffer) => {
      chunks.push(c);
      n += c.length;
      progress(n);
    });
    req.on('end', () => resolve(Buffer.concat(chunks)));
    req.on('error', reject);
  });
}
