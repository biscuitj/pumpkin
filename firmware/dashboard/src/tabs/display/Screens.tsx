import type { ComponentChildren } from 'preact';
import { useState } from 'preact/hooks';
import { clearAlerts, getAlertsClear, previewOnClock, toApiError } from '../../api';
import { DISPLAY_LIMITS, PETS, type DisplaySettings } from '../../contract';
import type { DisplaySettingsHook } from '../../lib/settings';
import { Segmented } from '../../ui/inputs';
import { NumberField } from '../../ui/NumberField';
import { Switch } from '../../ui/Switch';
import { PetSprite } from '../pet/PetSprite';

interface Props {
  hook: DisplaySettingsHook;
  s: DisplaySettings;
}

export function Screens({ hook, s }: Props) {
  const { update } = hook;
  const fieldError = (f: string) => (hook.error?.field === f ? hook.error.message : undefined);
  const now = new Date();
  const dd = String(now.getDate()).padStart(2, '0');
  const mm = String(now.getMonth() + 1).padStart(2, '0');

  return (
    <section class="section" aria-labelledby="screens-h">
      <div class="section__head">
        <h2 id="screens-h">Screens</h2>
        <p class="section__lede">The clock shows these in this order. Switch off the ones you don't need.</p>
      </div>

      <ul class="screens">
        <Row title="Glucose" desc="The reading in its range colour, with the trend arrow." fixed>
          <Switch
            id="delta-flash"
            checked={s.glucose.delta_flash}
            onChange={(v) => update({ glucose: { delta_flash: v } }, 0)}
            label="Show the change for 3 seconds when a new reading comes in"
          />
        </Row>

        <Row
          title="Time in range"
          desc="Share of the last 24 hours in range."
          on={s.screens.tir}
          onToggle={(v) => update({ screens: { tir: v } }, 0)}
        />

        <Row
          title="Active insulin"
          desc="Insulin on board from the pump, like 0.35U."
          on={s.screens.iob}
          onToggle={(v) => update({ screens: { iob: v } }, 0)}
        />

        <Row
          title="Pump alerts"
          desc="Battery, reservoir, sensor, set change and other pump alerts. Only shown while there is one."
          on={s.screens.alerts}
          onToggle={(v) => update({ screens: { alerts: v } }, 0)}
        >
          <Switch
            id="alerts-replace-pet"
            checked={s.alerts.replace_pet}
            disabled={!s.screens.pet}
            onChange={(v) => update({ alerts: { replace_pet: v } }, 0)}
            label="Show alerts in place of the pet"
            hint={s.screens.pet ? 'Otherwise they come right after it.' : 'Needs the pet screen.'}
          />
          <PreviewButton onClick={() => previewOnClock({ screen: 'alerts' })}>Preview alerts on the clock</PreviewButton>
          <ClearAlerts />
        </Row>

        <Row title="Time" desc="The time, with the date in between." on={s.screens.time} onToggle={(v) => update({ screens: { time: v } }, 0)}>
          <Switch
            id="hour24"
            checked={s.time.hour24}
            onChange={(v) => update({ time: { hour24: v } }, 0)}
            label="24-hour clock"
          />
          <Segmented
            name="date"
            label="Date"
            value={s.time.date}
            options={[
              { value: 'off', label: 'Off' },
              { value: 'day_month', label: `${dd}/${mm}` },
              { value: 'month_day', label: `${mm}/${dd}` },
            ]}
            onChange={(v) => update({ time: { date: v } }, 0)}
          />
        </Row>

        <Row title="Pet" desc="A pixel friend that reacts to the reading and sleeps at night." on={s.screens.pet} onToggle={(v) => update({ screens: { pet: v } }, 0)}>
          <p class="screen__more">
            <PetSprite id={s.pet.id} animate={false} />
            <span>
              {PETS.find((p) => p.id === s.pet.id)?.name}. Pick the pet and how it looks on the <a href="#/pet">Pet tab</a>.
            </span>
          </p>
        </Row>
      </ul>

      <div class="rotation">
        <Switch
          id="rotation-auto"
          checked={s.rotation.auto}
          onChange={(v) => update({ rotation: { auto: v } }, 0)}
          label="Change screens by themselves"
          hint={s.rotation.auto ? undefined : 'Only the buttons on the clock change screens.'}
        />
        {s.rotation.auto ? (
          <NumberField
            id="rotation-seconds"
            label="Show each screen for"
            value={s.rotation.seconds}
            min={DISPLAY_LIMITS.rotation_seconds.min}
            max={DISPLAY_LIMITS.rotation_seconds.max}
            unit="seconds"
            onCommit={(v) => update({ rotation: { seconds: v } }, 0)}
            error={fieldError('rotation.seconds')}
          />
        ) : null}
      </div>
    </section>
  );
}

interface RowProps {
  title: string;
  desc: string;
  fixed?: boolean;
  on?: boolean;
  onToggle?: (v: boolean) => void;
  children?: ComponentChildren;
}

/** One screen: switch, name and description; its options show while it's on. */
function Row({ title, desc, fixed, on = true, onToggle, children }: RowProps) {
  const id = `screen-${title.toLowerCase().replace(/\W+/g, '-')}`;
  return (
    <li class={on ? 'screen is-on' : 'screen'}>
      <div class="screen__head">
        {fixed ? (
          <span class="screen__fixed">Always on</span>
        ) : (
          <input
            id={id}
            class="switch"
            type="checkbox"
            role="switch"
            checked={on}
            onChange={(e) => onToggle?.(e.currentTarget.checked)}
            aria-describedby={`${id}-desc`}
          />
        )}
        <div class="screen__text">
          <label for={fixed ? undefined : id} class="screen__title">
            {title}
          </label>
          <p class="screen__desc" id={`${id}-desc`}>
            {desc}
          </p>
        </div>
      </div>
      {on && children ? <div class="screen__options">{children}</div> : null}
    </li>
  );
}

function PreviewButton({ onClick, children }: { onClick: () => Promise<void>; children: ComponentChildren }) {
  return (
    <button type="button" class="btn btn--small" onClick={() => void onClick().catch(() => {})}>
      {children}
    </button>
  );
}

/** Ask the bridge (through the clock) to forget remembered pump alerts. */
function ClearAlerts() {
  const [state, setState] = useState<'idle' | 'running' | 'done' | 'failed'>('idle');
  const [error, setError] = useState<string | null>(null);

  const run = async () => {
    setState('running');
    setError(null);
    try {
      await clearAlerts();
      for (let i = 0; i < 30; i++) {
        await new Promise((r) => setTimeout(r, 700));
        const s = await getAlertsClear();
        if (s.state === 'done' || s.state === 'failed') {
          setState(s.state);
          setError(s.error);
          return;
        }
      }
      throw new Error("The clock didn't hear back from the bridge.");
    } catch (e) {
      setState('failed');
      setError(toApiError(e).message);
    }
  };

  return (
    <div class="field">
      <div class="actions actions--tight">
        <button type="button" class="btn btn--small" onClick={() => void run()} disabled={state === 'running'}>
          {state === 'running' ? 'Clearing…' : 'Clear remembered alerts'}
        </button>
        {state === 'done' ? (
          <span class="actions__note" role="status">
            Cleared.
          </span>
        ) : null}
      </div>
      <p class={state === 'failed' ? 'field__msg field__msg--error' : 'field__msg'} role={state === 'failed' ? 'alert' : undefined}>
        {state === 'failed'
          ? `Couldn't clear them: ${error ?? 'unknown error'}`
          : "Battery, reservoir, sensor and set reminders stay until the pump data shows they're dealt with. This clears them now. Alerts still active on the pump come back until they're dismissed there."}
      </p>
    </div>
  );
}
