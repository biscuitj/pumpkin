import { useState } from 'preact/hooks';

const NO_AUTOFIX = { autocapitalize: 'off', autocorrect: 'off', spellcheck: false } as const;

interface TextProps {
  id: string;
  value: string;
  onInput: (v: string) => void;
  type?: 'text' | 'url';
  placeholder?: string;
  maxLength?: number;
  invalid?: boolean;
  autoComplete?: string;
}

export function TextInput({ id, value, onInput, type = 'text', invalid, autoComplete = 'off', ...rest }: TextProps) {
  const props = {
    id,
    class: 'input',
    value,
    onInput: (e: Event) => onInput((e.currentTarget as HTMLInputElement).value),
    'aria-invalid': invalid || undefined,
    'aria-describedby': `${id}-msg`,
    autoComplete,
    ...NO_AUTOFIX,
    ...rest,
  };
  // Preact's input typings are a union keyed on `type`, so each type is its own branch.
  return type === 'url' ? <input type="url" {...props} /> : <input type="text" {...props} />;
}

export function PasswordInput({ id, value, onInput, maxLength, invalid, autoComplete = 'off' }: Omit<TextProps, 'type'>) {
  const [shown, setShown] = useState(false);
  const props = {
    id,
    class: 'input',
    value,
    maxLength,
    onInput: (e: Event) => onInput((e.currentTarget as HTMLInputElement).value),
    'aria-invalid': invalid || undefined,
    'aria-describedby': `${id}-msg`,
    autoComplete,
    ...NO_AUTOFIX,
  };
  return (
    <div class="password">
      {shown ? <input type="text" {...props} /> : <input type="password" {...props} />}
      <button type="button" class="password__toggle" onClick={() => setShown(!shown)} aria-controls={id}>
        {shown ? 'Hide' : 'Show'}
      </button>
    </div>
  );
}

interface SegmentedProps<T extends string> {
  name: string;
  label: string;
  value: T;
  options: readonly { value: T; label: string }[];
  onChange: (v: T) => void;
}

/** A radio group drawn as a segmented control. */
export function Segmented<T extends string>({ name, label, value, options, onChange }: SegmentedProps<T>) {
  return (
    <fieldset class="seg">
      <legend class="field__label">{label}</legend>
      <div class="seg__track">
        {options.map((o) => (
          <label key={o.value} class={o.value === value ? 'seg__opt is-on' : 'seg__opt'}>
            <input type="radio" name={name} value={o.value} checked={o.value === value} onChange={() => onChange(o.value)} />
            <span>{o.label}</span>
          </label>
        ))}
      </div>
    </fieldset>
  );
}
