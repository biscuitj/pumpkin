import type { ComponentChildren } from 'preact';

interface Props {
  id: string;
  label: string;
  hint?: ComponentChildren;
  error?: string;
  children: ComponentChildren;
}

/** Label + control + one line of help, replaced by the error when there is one. */
export function Field({ id, label, hint, error, children }: Props) {
  return (
    <div class={error ? 'field field--error' : 'field'}>
      <label class="field__label" for={id}>
        {label}
      </label>
      {children}
      {error ? (
        <p class="field__msg field__msg--error" id={`${id}-msg`} role="alert">
          {error}
        </p>
      ) : hint ? (
        <p class="field__msg" id={`${id}-msg`}>
          {hint}
        </p>
      ) : null}
    </div>
  );
}
