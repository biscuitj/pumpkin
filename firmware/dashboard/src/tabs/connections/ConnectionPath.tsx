import type { Hop } from './describe';

/** The chain from the clock to the pump, so it's obvious where it breaks. */
export function ConnectionPath({ hops }: { hops: Hop[] }) {
  return (
    <ol class="path" aria-label="Connection from the clock to the pump">
      {hops.map((h) => (
        <li key={h.label} class={`path__hop path__hop--${h.tone}`}>
          <span class={`led led--${h.tone}`} aria-hidden="true" />
          <span class="path__label">{h.label}</span>
          <span class="path__detail">{h.detail}</span>
        </li>
      ))}
    </ol>
  );
}
