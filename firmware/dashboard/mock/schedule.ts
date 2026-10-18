/** Mock schedule: settings, PATCH validation and the "now" block, from the dev machine's clock. */
import type { IncomingMessage, ServerResponse } from 'node:http';
import { WEEKDAYS, type ScheduleSettings, type SchedulePatch, type ScheduleStatus, type Weekday } from '../src/contract.ts';
import { Invalid, readJson, send } from './http.ts';

const everyDay = (from: number, to: number) =>
  Object.fromEntries(WEEKDAYS.map((d) => [d, { from, to }])) as ScheduleSettings['awake'];

const settings: ScheduleSettings = {
  enabled: true,
  awake: everyDay(7 * 60, 21 * 60),
  night: 'dim',
  night_brightness: 10,
};

export async function handleSchedule(key: string, req: IncomingMessage, res: ServerResponse) {
  switch (key) {
    case 'GET /api/schedule':
      return send(res, 200, status());
    case 'PATCH /api/schedule': {
      const p = (await readJson(req)) as SchedulePatch;
      const next: ScheduleSettings = { ...settings, ...p, awake: { ...settings.awake, ...(p.awake ?? {}) } };
      validate(next);
      Object.assign(settings, next);
      return send(res, 200, status());
    }
    default:
      return send(res, 404, { error: `No such endpoint: ${key}` });
  }
}

function validate(s: ScheduleSettings) {
  for (const d of WEEKDAYS) {
    const h = s.awake[d];
    if (h === undefined) throw new Invalid('Every day needs hours or null.', `awake.${d}`);
    if (h === null) continue;
    const ok = (m: number) => Number.isInteger(m) && m >= 0 && m <= 1440;
    if (!ok(h.from) || !ok(h.to)) throw new Invalid('Times run from 00:00 to 24:00.', `awake.${d}`);
    if (h.from >= h.to) throw new Invalid('The end has to be after the start.', `awake.${d}`);
  }
  if (s.night !== 'dim' && s.night !== 'mute') throw new Invalid('Unknown night mode.', 'night');
  if (!Number.isInteger(s.night_brightness) || s.night_brightness < 1 || s.night_brightness > 255)
    throw new Invalid('Use 1 to 255.', 'night_brightness');
}

const weekdayOf = (d: Date): Weekday => WEEKDAYS[(d.getDay() + 6) % 7];

function awakeAt(day: Weekday, minute: number): boolean {
  const h = settings.awake[day];
  return !!h && minute >= h.from && minute < h.to;
}

function status(): ScheduleStatus {
  const d = new Date();
  const weekday = weekdayOf(d);
  const minute = d.getHours() * 60 + d.getMinutes();
  if (!settings.enabled) {
    return { settings, now: { time_valid: true, weekday, minute, state: 'off', next_change: null } };
  }
  const awake = awakeAt(weekday, minute);

  // Walk forward minute by minute (cheap enough for a mock) to the next switch.
  let next_change: ScheduleStatus['now']['next_change'] = null;
  for (let i = 1; i <= 7 * 1440; i++) {
    const day = WEEKDAYS[(WEEKDAYS.indexOf(weekday) + Math.floor((minute + i) / 1440)) % 7];
    const m = (minute + i) % 1440;
    if (awakeAt(day, m) !== awake) {
      next_change = { weekday: day, minute: m, to: awake ? 'night' : 'awake' };
      break;
    }
  }
  return { settings, now: { time_valid: true, weekday, minute, state: awake ? 'awake' : 'night', next_change } };
}
