import type { DeviceInfo, ResetReason } from '../../contract';
import { age } from '../../lib/format';

const REASONS: Record<ResetReason, string> = {
  power_on: 'Plugged in',
  software: 'Restarted from this page or after an update',
  watchdog: 'Got stuck and restarted itself (watchdog)',
  panic: 'Crashed and restarted',
  brownout: 'The power dipped too low',
  other: 'Other',
};

const kb = (n: number) => `${Math.round(n / 1024)} KB`;

interface Props {
  dev: DeviceInfo;
  open: boolean;
  onToggle: (open: boolean) => void;
}

/** For support: what the clock reports about itself. Polls fast while open. */
export function Diagnostics({ dev, open, onToggle }: Props) {
  const g = dev.diagnostics;
  const rows: [string, string][] = [
    ['Running for', age(g.uptime_sec)],
    ['Last restart', REASONS[g.reset_reason]],
    ['Free memory', `${kb(g.free_heap)} (lowest ${kb(g.min_free_heap)}, largest block ${kb(g.largest_free_block)})`],
    ['Light sensor', `${g.light} of 4095`],
    ['Brightness now', `${Math.round((g.brightness_now / 255) * 100)}%`],
    ['Last fetch error', g.last_fetch_error ?? 'None'],
  ];

  return (
    <details class="section diag" open={open} onToggle={(e) => onToggle((e.currentTarget as HTMLDetailsElement).open)}>
      <summary class="diag__summary">
        <h2>Diagnostics</h2>
      </summary>
      <dl class="diag__list">
        {rows.map(([k, v]) => (
          <div key={k} class="diag__row">
            <dt>{k}</dt>
            <dd>{v}</dd>
          </div>
        ))}
      </dl>
      <div class="field">
        <span class="field__label">Button test</span>
        <p class="field__msg">Press the buttons on the clock: they light up here.</p>
        <div class="buttons-test">
          {(['left', 'middle', 'right'] as const).map((b) => (
            <span key={b} class={g.buttons[b] ? 'btn-key is-down' : 'btn-key'}>
              <span class={`led led--${g.buttons[b] ? 'ok' : 'idle'}`} aria-hidden="true" />
              {b[0].toUpperCase() + b.slice(1)}
              <span class="sr-only">{g.buttons[b] ? ' pressed' : ''}</span>
            </span>
          ))}
        </div>
      </div>
    </details>
  );
}
