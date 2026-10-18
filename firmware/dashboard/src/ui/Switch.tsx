import type { ComponentChildren } from 'preact';

interface Props {
  id: string;
  checked: boolean;
  onChange: (v: boolean) => void;
  label: ComponentChildren;
  hint?: ComponentChildren;
  disabled?: boolean;
}

/** On/off setting: a checkbox with role="switch", label and optional help line. */
export function Switch({ id, checked, onChange, label, hint, disabled }: Props) {
  return (
    <div class="switch-row">
      <input
        id={id}
        class="switch"
        type="checkbox"
        role="switch"
        checked={checked}
        disabled={disabled}
        onChange={(e) => onChange(e.currentTarget.checked)}
        aria-describedby={hint ? `${id}-hint` : undefined}
      />
      <div class="switch-row__text">
        <label for={id} class="switch-row__label">
          {label}
        </label>
        {hint ? (
          <p class="field__msg" id={`${id}-hint`}>
            {hint}
          </p>
        ) : null}
      </div>
    </div>
  );
}
