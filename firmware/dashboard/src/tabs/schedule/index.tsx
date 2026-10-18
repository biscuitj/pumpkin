import './schedule.css';
import { useEffect, useState } from 'preact/hooks';
import { getSchedule } from '../../api';
import { WEEKDAYS, type AwakeHours, type ScheduleStatus, type Weekday } from '../../contract';
import { usePoll } from '../../lib/poll';
import { useSchedule } from '../../lib/settings';
import { Segmented } from '../../ui/inputs';
import { Notice, type Tone } from '../../ui/Notice';
import { Switch } from '../../ui/Switch';

const DAY_NAMES: Record<Weekday, string> = {
  mon: 'Monday',
  tue: 'Tuesday',
  wed: 'Wednesday',
  thu: 'Thursday',
  fri: 'Friday',
  sat: 'Saturday',
  sun: 'Sunday',
};
const SHORT: Record<Weekday, string> = { mon: 'Mon', tue: 'Tue', wed: 'Wed', thu: 'Thu', fri: 'Fri', sat: 'Sat', sun: 'Sun' };

const pad = (n: number) => String(n).padStart(2, '0');
/** 1440 is shown as 00:00: as an end time it means midnight. */
const toHHMM = (m: number) => `${pad(Math.floor(m / 60) % 24)}:${pad(m % 60)}`;
function fromHHMM(s: string, isEnd: boolean): number | null {
  const m = /^(\d{2}):(\d{2})$/.exec(s);
  if (!m) return null;
  const v = Number(m[1]) * 60 + Number(m[2]);
  return isEnd && v === 0 ? 1440 : v;
}

const fill = (hours: AwakeHours | null, days: readonly Weekday[] = WEEKDAYS) =>
  Object.fromEntries(days.map((d) => [d, hours])) as Partial<Record<Weekday, AwakeHours | null>>;

export function ScheduleTab() {
  const hook = useSchedule();
  // `now` moves on by itself; the settings come from the autosave hook.
  const live = usePoll(getSchedule, 30_000);
  const v = hook.settings;
  // After a save, ask again: the status line depends on what just changed.
  useEffect(() => {
    if (hook.saveState === 'saved') live.refresh();
  }, [hook.saveState, hook.settings]);

  if (hook.loadError) {
    return (
      <div class="tab">
        <Notice tone="fail" title="Couldn't load the schedule.">
          {hook.loadError}{' '}
          <button type="button" class="btn btn--quiet" onClick={hook.reload}>
            Try again
          </button>
        </Notice>
      </div>
    );
  }
  if (!v) return <p class="muted">Asking the clock…</p>;

  const s = v.settings;
  const now = live.data?.now ?? v.now;
  const status = describeNow(now, s.night);
  const dayError = (d: Weekday) => (hook.error?.field === `awake.${d}` ? hook.error.message : undefined);

  return (
    <div class="tab">
      <section class="section" aria-labelledby="schedule-h">
        <div class="section__head">
          <h2 id="schedule-h">Awake hours</h2>
          <p class="section__lede">
            When the clock should be on. Outside these hours it goes into night mode and the pet sleeps. Readings keep
            coming in either way.
          </p>
        </div>

        <Notice tone={status.tone} title={status.title} live>
          {status.body}
        </Notice>

        <Switch
          id="schedule-enabled"
          checked={s.enabled}
          onChange={(on) => hook.update({ enabled: on }, 0)}
          label="Use a schedule"
          hint={s.enabled ? undefined : 'The clock stays awake around the clock.'}
        />
        <p class={`autosave autosave--${hook.saveState}`} role="status">
          {hook.saveState === 'saving' ? 'Saving…' : hook.saveState === 'saved' ? 'Saved on the clock.' : 'Changes apply right away.'}
        </p>
        {hook.error && !hook.error.field?.startsWith('awake.') ? <Notice tone="fail" title={hook.error.message} live /> : null}

        {s.enabled ? (
          <>
            <div class="actions actions--tight">
              <span class="muted">Start from:</span>
              <button type="button" class="btn btn--small" onClick={() => hook.update({ awake: fill({ from: 420, to: 1260 }) }, 0)}>
                Home, 07:00–21:00
              </button>
              <button
                type="button"
                class="btn btn--small"
                onClick={() =>
                  hook.update({ awake: { ...fill({ from: 495, to: 945 }, WEEKDAYS.slice(0, 5)), sat: null, sun: null } }, 0)
                }
              >
                School days, 08:15–15:45
              </button>
            </div>

            <ol class="week" aria-label="Awake hours per day">
              {WEEKDAYS.map((d) => (
                <Day
                  key={d}
                  day={d}
                  hours={s.awake[d]}
                  today={now.weekday === d}
                  minute={now.weekday === d ? now.minute : null}
                  error={dayError(d)}
                  onChange={(h) => hook.update({ awake: { [d]: h } }, 0)}
                />
              ))}
            </ol>
            <p class="muted week__note">An end time of 00:00 means midnight.</p>
            <CopyDay onCopy={(d) => hook.update({ awake: fill(s.awake[d]) }, 0)} />
          </>
        ) : null}
      </section>

      {s.enabled ? (
        <section class="section" aria-labelledby="night-h">
          <div class="section__head">
            <h2 id="night-h">Night mode</h2>
          </div>
          <Segmented
            name="night"
            label="Outside awake hours"
            value={s.night}
            options={[
              { value: 'dim', label: 'Dim the screen' },
              { value: 'mute', label: 'Dark and silent' },
            ]}
            onChange={(night) => hook.update({ night }, 0)}
          />
          {s.night === 'mute' ? (
            <Notice tone="warn" title="Nothing wakes the clock at night, not even an urgent low.">
              The screen is black and the buzzer is off. Use this for a clock nobody watches outside these hours, like
              one at school.
            </Notice>
          ) : (
            <div class="field">
              <label class="field__label" for="night-brightness">
                Night brightness
              </label>
              <div class="slider">
                <input
                  id="night-brightness"
                  type="range"
                  min={1}
                  max={255}
                  value={s.night_brightness}
                  onInput={(e) => hook.update({ night_brightness: Number(e.currentTarget.value) }, 300)}
                />
                <span class="slider__value">{Math.round((s.night_brightness / 255) * 100)}%</span>
              </div>
            </div>
          )}
        </section>
      ) : null}
    </div>
  );
}

interface DayProps {
  day: Weekday;
  hours: AwakeHours | null;
  today: boolean;
  minute: number | null;
  error?: string;
  onChange: (h: AwakeHours | null) => void;
}

function Day({ day, hours, today, minute, error, onChange }: DayProps) {
  // Last good hours, so switching a day off and on again brings them back.
  const [remembered, setRemembered] = useState<AwakeHours>(hours ?? { from: 420, to: 1260 });
  const [local, setLocal] = useState<string | null>(null);
  const id = `day-${day}`;

  const set = (part: 'from' | 'to', value: string) => {
    if (!hours) return;
    const m = fromHHMM(value, part === 'to');
    if (m === null) return;
    const next = { ...hours, [part]: m };
    if (next.from >= next.to) return setLocal('The end has to be after the start.');
    setLocal(null);
    setRemembered(next);
    onChange(next);
  };

  const err = local ?? error;

  return (
    <li class={`day${today ? ' day--today' : ''}${hours ? '' : ' day--off'}`}>
      <div class="day__row">
        <input
          id={id}
          class="switch"
          type="checkbox"
          role="switch"
          checked={!!hours}
          onChange={(e) => onChange(e.currentTarget.checked ? remembered : null)}
        />
        <label for={id} class="day__name">
          <span class="day__long">{DAY_NAMES[day]}</span>
          <span class="day__short">{SHORT[day]}</span>
          {today ? <span class="day__today">today</span> : null}
        </label>
        {hours ? (
          <span class="day__times">
            <input
              type="time"
              class="input input--time"
              value={toHHMM(hours.from)}
              onChange={(e) => set('from', e.currentTarget.value)}
              aria-label={`${DAY_NAMES[day]}: awake from`}
            />
            <span aria-hidden="true">–</span>
            <input
              type="time"
              class="input input--time"
              value={toHHMM(hours.to)}
              onChange={(e) => set('to', e.currentTarget.value)}
              aria-label={`${DAY_NAMES[day]}: awake until`}
            />
          </span>
        ) : (
          <span class="day__asleep muted">Night all day</span>
        )}
      </div>
      <DayBar hours={hours} minute={minute} />
      {err ? (
        <p class="field__msg field__msg--error" role="alert">
          {err}
        </p>
      ) : null}
    </li>
  );
}

/** Use one day's hours (or its night-all-day) for the whole week. */
function CopyDay({ onCopy }: { onCopy: (d: Weekday) => void }) {
  const [day, setDay] = useState<Weekday>('mon');
  return (
    <div class="copyday">
      <label for="copy-day">Use the hours of</label>
      <select id="copy-day" class="input input--inline" value={day} onChange={(e) => setDay(e.currentTarget.value as Weekday)}>
        {WEEKDAYS.map((d) => (
          <option key={d} value={d}>
            {DAY_NAMES[d]}
          </option>
        ))}
      </select>
      <button type="button" class="btn btn--small" onClick={() => onCopy(day)}>
        for every day
      </button>
    </div>
  );
}

/** 24 hours as a strip: awake lit, night dark, a marker at the current time today. */
function DayBar({ hours, minute }: { hours: AwakeHours | null; minute: number | null }) {
  const pct = (m: number) => `${(m / 1440) * 100}%`;
  return (
    <div class="daybar" aria-hidden="true">
      {hours ? <span class="daybar__awake" style={{ left: pct(hours.from), width: pct(hours.to - hours.from) }} /> : null}
      {minute !== null ? <span class="daybar__now" style={{ left: pct(minute) }} /> : null}
    </div>
  );
}

function describeNow(now: ScheduleStatus['now'], night: 'dim' | 'mute'): { tone: Tone; title: string; body?: string } {
  if (now.state === 'off') return { tone: 'ok', title: 'Always awake.' };
  if (!now.time_valid)
    return {
      tone: 'warn',
      title: "The schedule waits until the clock knows the time.",
      body: 'Until then the clock stays awake. Check time sync on the Connections tab.',
    };
  const next = now.next_change;
  const when = next ? `${next.weekday === now.weekday ? 'today' : DAY_NAMES[next.weekday]} at ${toHHMM(next.minute)}` : null;
  if (now.state === 'awake')
    return { tone: 'ok', title: 'Awake now.', body: when ? `Night mode starts ${when}.` : 'No night mode in the coming week.' };
  return {
    tone: 'idle',
    title: night === 'mute' ? 'Night mode now: dark and silent.' : 'Night mode now: dimmed.',
    body: when ? `Awake again ${when}.` : undefined,
  };
}
