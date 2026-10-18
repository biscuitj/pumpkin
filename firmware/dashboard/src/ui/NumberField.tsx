import { useEffect, useState } from 'preact/hooks';

interface Props {
  id: string;
  label: string;
  value: number;
  /** Called with a valid value when the field is left or Enter is pressed. */
  onCommit: (v: number) => void;
  min: number;
  max: number;
  decimals?: number;
  unit?: string;
  hint?: string;
  /** Extra check after min/max; return a message to refuse the value. */
  check?: (v: number) => string | null;
  /** Error from the clock for this field. */
  error?: string;
}

/**
 * A number that's only sent once it's complete: typing "1" on the way to "180"
 * never reaches the clock. Out-of-range values show an error and keep the old one.
 */
export function NumberField({ id, label, value, onCommit, min, max, decimals = 0, unit, hint, check, error }: Props) {
  const shown = value.toFixed(decimals);
  const [text, setText] = useState(shown);
  const [localError, setLocalError] = useState<string | null>(null);

  // Follow the value when it changes elsewhere (unit switch, server revert).
  useEffect(() => setText(shown), [shown]);

  const commit = () => {
    const v = Number(text.replace(',', '.'));
    if (text.trim() === '' || Number.isNaN(v)) return setLocalError('Enter a number.');
    if (v < min || v > max)
      return setLocalError(`Use ${min.toFixed(decimals)} to ${max.toFixed(decimals)}${unit ? ` ${unit}` : ''}.`);
    const rounded = Number(v.toFixed(decimals));
    const problem = check?.(rounded);
    if (problem) return setLocalError(problem);
    setLocalError(null);
    if (rounded.toFixed(decimals) !== shown) onCommit(rounded);
    setText(rounded.toFixed(decimals));
  };

  const err = localError ?? error;

  return (
    <div class={err ? 'field field--error' : 'field'}>
      <label class="field__label" for={id}>
        {label}
      </label>
      <div class="number">
        <input
          id={id}
          class="input number__input"
          type="text"
          inputMode={decimals ? 'decimal' : 'numeric'}
          value={text}
          onInput={(e) => {
            setText(e.currentTarget.value);
            setLocalError(null);
          }}
          onBlur={commit}
          onKeyDown={(e) => e.key === 'Enter' && (e.preventDefault(), commit())}
          aria-invalid={err ? true : undefined}
          aria-describedby={`${id}-msg`}
        />
        {unit ? <span class="number__unit">{unit}</span> : null}
      </div>
      {err ? (
        <p class="field__msg field__msg--error" id={`${id}-msg`} role="alert">
          {err}
        </p>
      ) : hint ? (
        <p class="field__msg" id={`${id}-msg`}>
          {hint}
        </p>
      ) : null}
    </div>
  );
}
