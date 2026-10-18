import type { ComponentChildren } from 'preact';

export type Tone = 'ok' | 'warn' | 'fail' | 'idle';

interface Props {
  tone: Tone;
  title: ComponentChildren;
  children?: ComponentChildren;
  live?: boolean;
}

/** A status message with an LED-style marker in the tone's colour. */
export function Notice({ tone, title, children, live }: Props) {
  return (
    <div class={`notice notice--${tone}`} role={live ? 'status' : undefined}>
      <span class={`led led--${tone}`} aria-hidden="true" />
      <div class="notice__text">
        <p class="notice__title">{title}</p>
        {children ? <div class="notice__body">{children}</div> : null}
      </div>
    </div>
  );
}
