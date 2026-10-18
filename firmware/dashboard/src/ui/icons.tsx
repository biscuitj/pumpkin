/** Four bars, filled by signal strength. */
export function SignalBars({ rssi }: { rssi: number | null }) {
  const level = rssi === null ? 0 : rssi > -55 ? 4 : rssi > -65 ? 3 : rssi > -75 ? 2 : 1;
  const label = ['No signal', 'Weak signal', 'Fair signal', 'Good signal', 'Strong signal'][level];
  return (
    <svg class="bars" viewBox="0 0 16 14" width="16" height="14" role="img" aria-label={label}>
      {[0, 1, 2, 3].map((i) => (
        <rect key={i} x={i * 4} y={10 - i * 3} width="3" height={4 + i * 3} rx="0.75" class={i < level ? 'bars__on' : 'bars__off'} />
      ))}
    </svg>
  );
}

export function Lock() {
  return (
    <svg class="icon" viewBox="0 0 12 14" width="11" height="13" aria-hidden="true">
      <rect x="1" y="6" width="10" height="7.5" rx="1.5" fill="currentColor" />
      <path d="M3.5 6V4.2a2.5 2.5 0 0 1 5 0V6" fill="none" stroke="currentColor" stroke-width="1.5" />
    </svg>
  );
}
