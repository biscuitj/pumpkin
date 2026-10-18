import './display.css';
import { DISPLAY_LIMITS } from '../../contract';
import { useDisplaySettings } from '../../lib/settings';
import { Notice } from '../../ui/Notice';
import { Switch } from '../../ui/Switch';
import { Glucose } from './Glucose';
import { LiveTop } from './LiveTop';
import { Screens } from './Screens';

export function DisplayTab() {
  const hook = useDisplaySettings();
  const s = hook.settings;

  return (
    <div class="tab">
      <LiveTop hook={hook} />

      {hook.loadError ? (
        <Notice tone="fail" title="Couldn't load the display settings.">
          {hook.loadError}{' '}
          <button type="button" class="btn btn--quiet" onClick={hook.reload}>
            Try again
          </button>
        </Notice>
      ) : !s ? (
        <p class="muted">Asking the clock…</p>
      ) : (
        <>
          <Screens hook={hook} s={s} />
          <Glucose hook={hook} s={s} />
          <section class="section" aria-labelledby="brightness-h">
            <div class="section__head">
              <h2 id="brightness-h">Brightness</h2>
            </div>
            <Switch
              id="brightness-auto"
              checked={s.brightness.auto}
              onChange={(v) => hook.update({ brightness: { auto: v } }, 0)}
              label="Follow the light in the room"
              hint="Uses the light sensor on top of the clock."
            />
            {s.brightness.auto ? null : (
              <div class="field">
                <label class="field__label" for="brightness-level">
                  Brightness
                </label>
                <div class="slider">
                  <input
                    id="brightness-level"
                    type="range"
                    min={DISPLAY_LIMITS.brightness.min}
                    max={DISPLAY_LIMITS.brightness.max}
                    value={s.brightness.level}
                    onInput={(e) => hook.update({ brightness: { level: Number(e.currentTarget.value) } }, 300)}
                  />
                  <span class="slider__value">{Math.round((s.brightness.level / 255) * 100)}%</span>
                </div>
              </div>
            )}
          </section>
        </>
      )}
    </div>
  );
}
