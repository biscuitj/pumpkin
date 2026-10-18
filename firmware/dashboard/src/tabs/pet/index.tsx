import '../display/display.css';
import { previewOnClock } from '../../api';
import { PETS, type PetId } from '../../contract';
import { useDisplaySettings } from '../../lib/settings';
import { Segmented } from '../../ui/inputs';
import { Notice } from '../../ui/Notice';
import { Switch } from '../../ui/Switch';
import { LiveTop } from '../display/LiveTop';
import { PetSprite } from './PetSprite';

const PET_STYLES = [
  { value: 'text', label: 'With LOW, OK, HIGH' },
  { value: 'icon', label: 'With an icon' },
  { value: 'pet_only', label: 'Pet only' },
] as const;

const RANGES = [
  { range: 'low', label: 'Show low' },
  { range: 'in_range', label: 'Show in range' },
  { range: 'high', label: 'Show high' },
] as const;

const show = (body: Parameters<typeof previewOnClock>[0]) => void previewOnClock(body).catch(() => {});

export function PetTab() {
  const hook = useDisplaySettings();
  const { update } = hook;
  const s = hook.settings;

  const pick = (id: PetId) => {
    update({ pet: { id } }, 0);
    show({ screen: 'pet', pet: id, range: 'in_range' });
  };

  return (
    <div class="tab">
      <LiveTop hook={hook} />

      {hook.loadError ? (
        <Notice tone="fail" title="Couldn't load the pet settings.">
          {hook.loadError}{' '}
          <button type="button" class="btn btn--quiet" onClick={hook.reload}>
            Try again
          </button>
        </Notice>
      ) : !s ? (
        <p class="muted">Asking the clock…</p>
      ) : (
        <>
          <section class="section" aria-labelledby="pet-h">
            <div class="section__head">
              <h2 id="pet-h">Pet</h2>
              <p class="section__lede">
                A pixel friend between the other screens. It's happy in range, droopy when low, wound up when high, and
                sleeps at night.
              </p>
            </div>
            {s.screens.pet ? null : (
              <Notice tone="warn" title="The pet screen is off.">
                The clock doesn't show the pet until it's switched on under <a href="#/display">Display</a>.
              </Notice>
            )}

            <fieldset class="pets">
              <legend class="field__label">Which pet</legend>
              <div class="pets__grid">
                {PETS.map((p) => (
                  <label key={p.id} class={p.id === s.pet.id ? 'pet is-on' : 'pet'}>
                    <input type="radio" name="pet" value={p.id} checked={p.id === s.pet.id} onChange={() => pick(p.id)} />
                    <PetSprite id={p.id} animate={p.id === s.pet.id} />
                    <span class="pet__name">{p.name}</span>
                    <span class="pet__kind">{p.kind}</span>
                  </label>
                ))}
              </div>
              <p class="field__msg">Picking one shows it on the clock for a few seconds.</p>
            </fieldset>
          </section>

          <section class="section" aria-labelledby="pet-look-h">
            <div class="section__head">
              <h2 id="pet-look-h">On the screen</h2>
            </div>
            <Segmented
              name="pet-style"
              label="Next to the pet"
              value={s.pet.style}
              options={PET_STYLES}
              onChange={(v) => update({ pet: { style: v } }, 0)}
            />
            {s.pet.style === 'text' ? (
              <Switch
                id="pet-tir"
                checked={s.pet.show_tir}
                onChange={(v) => update({ pet: { show_tir: v } }, 0)}
                label="Show time in range instead of LOW, OK, HIGH"
              />
            ) : null}
            <Switch
              id="pet-seasonal"
              checked={s.pet.seasonal}
              onChange={(v) => update({ pet: { seasonal: v } }, 0)}
              label="Seasonal surprises"
              hint="With the pet on its own: something for Halloween, fireworks at New Year."
            />
            <div class="field">
              <span class="field__label">Try it on the clock</span>
              <div class="actions actions--tight">
                {RANGES.map((r) => (
                  <button
                    key={r.range}
                    type="button"
                    class="btn btn--small"
                    onClick={() => show({ screen: 'pet', pet: s.pet.id, range: r.range })}
                  >
                    {r.label}
                  </button>
                ))}
              </div>
              <p class="field__msg">Shows each pose for 8 seconds, never over an urgent reading.</p>
            </div>
          </section>
        </>
      )}
    </div>
  );
}
