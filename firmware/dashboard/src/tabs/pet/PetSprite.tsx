import './pet.css';
import { signal } from '@preact/signals';
import { useEffect, useState } from 'preact/hooks';
import { getPetSprites } from '../../api';
import type { PetId, PetSprites } from '../../contract';

// Fetched once per page: the pets don't change while it's open.
const sprites = signal<PetSprites | null>(null);
let requested = false;
function load() {
  if (requested) return;
  requested = true;
  getPetSprites()
    .then((s) => (sprites.value = s))
    .catch(() => (requested = false)); // try again next time a picker mounts
}

const reducedMotion = () => window.matchMedia?.('(prefers-reduced-motion: reduce)').matches ?? false;

/**
 * A pet as the clock draws it, from the clock's own pixel data. `animate`
 * plays its in-range idle pose (the leaf flutters, the cat's tail swishes).
 */
export function PetSprite({ id, animate }: { id: PetId; animate: boolean }) {
  useEffect(load, []);
  const s = sprites.value;
  const pet = s?.pets.find((p) => p.id === id);
  const [frame, setFrame] = useState(0);

  useEffect(() => {
    setFrame(0);
    if (!animate || !s || reducedMotion()) return;
    const timer = setInterval(() => setFrame((f) => (f + 1) % 2), s.frame_ms);
    return () => clearInterval(timer);
  }, [animate, s]);

  if (!s) return <span class="pet__sprite" aria-hidden="true" />;
  if (!pet) return null;
  const rows = pet.frames[frame] ?? pet.frames[0];
  const width = rows[0]?.length ?? 0;
  const pixels: { x: number; y: number; c: string }[] = [];
  rows.forEach((row, y) => {
    for (let x = 0; x < row.length; x++) if (row[x] !== '.') pixels.push({ x, y, c: s.colors[row[x]] ?? '#000' });
  });

  return (
    <span class="pet__sprite" aria-hidden="true">
      <svg viewBox={`-1 -1 ${width + 2} 10`} shape-rendering="crispEdges">
        {pixels.map((p) => (
          <rect key={`${p.x}-${p.y}`} x={p.x + 0.06} y={p.y + 0.06} width="0.88" height="0.88" rx="0.18" fill={p.c} />
        ))}
      </svg>
    </span>
  );
}
