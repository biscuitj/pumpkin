import { COLOR_PRESETS, DISPLAY_LIMITS, type ColorPreset, type DisplaySettings, type GlucoseBand } from '../../contract';
import type { DisplaySettingsHook } from '../../lib/settings';
import { Segmented } from '../../ui/inputs';
import { NumberField } from '../../ui/NumberField';
import { Switch } from '../../ui/Switch';

const MMOL = 18.016;
type Threshold = 'urgent_low' | 'low' | 'high' | 'urgent_high';

const THRESHOLDS: { key: Threshold; label: string }[] = [
  { key: 'urgent_low', label: 'Urgent low below' },
  { key: 'low', label: 'Low below' },
  { key: 'high', label: 'High above' },
  { key: 'urgent_high', label: 'Urgent high above' },
];

const PRESET_NOTES: Record<ColorPreset, string> = {
  classic: 'Green in range, amber out of range, red when urgent.',
  colorblind: 'Blue in range, warm when low, purple when high. No red and green to tell apart.',
  calm: 'White in range. Colour only when something needs attention.',
};

interface Props {
  hook: DisplaySettingsHook;
  s: DisplaySettings;
}

export function Glucose({ hook, s }: Props) {
  const { update } = hook;
  const g = s.glucose;
  const mmol = g.unit === 'mmol';
  const unit = mmol ? 'mmol/L' : 'mg/dL';
  const toShown = (mgdl: number) => (mmol ? mgdl / MMOL : mgdl);
  const toMgdl = (v: number) => (mmol ? Math.round(v * MMOL) : v);
  const fieldError = (f: string) => (hook.error?.field === f ? hook.error.message : undefined);
  const lim = DISPLAY_LIMITS.glucose_mgdl;

  /** Keep urgent low < low < high < urgent high, checked in mg/dL. */
  const order = (key: Threshold) => (shown: number) => {
    const v = toMgdl(shown);
    const next = { ...g, [key]: v };
    if (!(next.urgent_low < next.low)) return 'Urgent low has to be below low.';
    if (!(next.low < next.high)) return 'Low has to be below high.';
    if (!(next.high < next.urgent_high)) return 'Urgent high has to be above high.';
    return null;
  };

  return (
    <section class="section" aria-labelledby="glucose-h">
      <div class="section__head">
        <h2 id="glucose-h">Glucose</h2>
        <p class="section__lede">How readings look: units, where the ranges start and which colours they get.</p>
      </div>

      <Segmented
        name="unit"
        label="Units"
        value={g.unit}
        options={[
          { value: 'mgdl', label: 'mg/dL' },
          { value: 'mmol', label: 'mmol/L' },
        ]}
        onChange={(v) => update({ glucose: { unit: v } }, 0)}
      />

      <div class="field">
        <span class="field__label">Ranges</span>
        <RangeBar g={g} />
        <div class="thresholds">
          {THRESHOLDS.map((t) => (
            <NumberField
              key={`${t.key}-${g.unit}`}
              id={`th-${t.key}`}
              label={t.label}
              value={toShown(g[t.key])}
              decimals={mmol ? 1 : 0}
              min={toShown(lim.min)}
              max={toShown(lim.max)}
              unit={unit}
              check={order(t.key)}
              onCommit={(v) => update({ glucose: { [t.key]: toMgdl(v) } }, 0)}
              error={fieldError(`glucose.${t.key}`)}
            />
          ))}
        </div>
      </div>

      <fieldset class="presets">
        <legend class="field__label">Colours</legend>
        {(Object.keys(COLOR_PRESETS) as ColorPreset[]).map((id) => {
          const p = COLOR_PRESETS[id];
          return (
            <label key={id} class={id === g.colors ? 'preset is-on' : 'preset'}>
              <input type="radio" name="colors" value={id} checked={id === g.colors} onChange={() => update({ glucose: { colors: id } }, 0)} />
              <span class="preset__leds" aria-hidden="true">
                {(['urgent_low', 'low', 'in_range', 'high', 'urgent_high'] as GlucoseBand[]).map((b) => (
                  <span key={b} class="preset__led" style={{ '--c': p.colors[b] }} />
                ))}
              </span>
              <span class="preset__text">
                <span class="preset__name">{p.name}</span>
                <span class="preset__note">{PRESET_NOTES[id]}</span>
              </span>
            </label>
          );
        })}
      </fieldset>

      <NumberField
        id="stale-after"
        label="Grey out a reading after"
        value={g.stale_after_min}
        min={DISPLAY_LIMITS.stale_after_min.min}
        max={DISPLAY_LIMITS.stale_after_min.max}
        unit="minutes"
        hint={`From ${Math.floor(g.stale_after_min / 2)} minutes the reading dims and shows an exclamation mark, so nobody trusts an old number.`}
        onCommit={(v) => update({ glucose: { stale_after_min: v } }, 0)}
        error={fieldError('glucose.stale_after_min')}
      />

      <Switch
        id="low-lock"
        checked={g.low_lock}
        onChange={(v) => update({ glucose: { low_lock: v } }, 0)}
        label="Keep the reading on screen while low"
        hint="Below Low, the clock shows only the reading. Other screens and the buttons wait until it's back up."
      />
    </section>
  );
}

const SCALE = { min: 40, max: 350 };

/** The four thresholds on one strip, drawn in the chosen colours on a dark face like the clock's. */
function RangeBar({ g }: { g: DisplaySettings['glucose'] }) {
  const c = COLOR_PRESETS[g.colors].colors;
  const x = (v: number) => ((Math.min(Math.max(v, SCALE.min), SCALE.max) - SCALE.min) / (SCALE.max - SCALE.min)) * 100;
  const bands: { from: number; to: number; color: string }[] = [
    { from: SCALE.min, to: g.urgent_low, color: c.urgent_low },
    { from: g.urgent_low, to: g.low, color: c.low },
    { from: g.low, to: g.high, color: c.in_range },
    { from: g.high, to: g.urgent_high, color: c.high },
    { from: g.urgent_high, to: SCALE.max, color: c.urgent_high },
  ];
  const fmt = (v: number) => (g.unit === 'mmol' ? (v / MMOL).toFixed(1) : String(v));

  return (
    <div class="rangebar" aria-hidden="true">
      <div class="rangebar__strip">
        {bands.map((b, i) => (
          <span key={i} class="rangebar__band" style={{ left: `${x(b.from)}%`, width: `${x(b.to) - x(b.from)}%`, '--c': b.color }} />
        ))}
      </div>
      <div class="rangebar__ticks">
        {[g.urgent_low, g.low, g.high, g.urgent_high].map((v, i) => (
          <span key={i} class={i === 0 || i === 3 ? 'rangebar__tick rangebar__tick--urgent' : 'rangebar__tick'} style={{ left: `${x(v)}%` }}>
            {fmt(v)}
          </span>
        ))}
      </div>
    </div>
  );
}
