import { useEffect, useRef, useState } from 'preact/hooks';
import { displayNext, displayPrev, getFrame, toApiError } from '../../api';

const LABELS: Record<string, string> = {
  glucose: 'Glucose',
  tir: 'Time in range',
  iob: 'Active insulin',
  alerts: 'Pump alerts',
  time: 'Time',
  pet: 'Pet',
  status: 'Status message',
  boot: 'Starting up',
  update: 'Installing an update',
};

const FRAME_MS = 400;
const W = 32;
const H = 8;
const PITCH = 40; // canvas px per LED; CSS scales it down

/** Live copy of the clock's 32×8 matrix, with previous/next. */
export function Mirror() {
  const canvas = useRef<HTMLCanvasElement>(null);
  const [screen, setScreen] = useState<string | null>(null);
  const [error, setError] = useState<string | null>(null);
  const kick = useRef<() => void>(() => {});

  useEffect(() => {
    let alive = true;
    let etag: string | null = null;
    let timer: ReturnType<typeof setTimeout> | undefined;

    const tick = async () => {
      clearTimeout(timer);
      if (!document.hidden) {
        try {
          const f = await getFrame(etag);
          if (f && alive && canvas.current) {
            etag = f.etag;
            draw(canvas.current, f.rgb);
            setScreen(f.screen);
          }
          if (alive) setError(null);
        } catch (e) {
          if (alive) setError(toApiError(e).message);
        }
      }
      if (alive) timer = setTimeout(tick, FRAME_MS);
    };

    kick.current = () => void tick();
    if (canvas.current) draw(canvas.current, new Uint8Array(W * H * 3));
    void tick();
    return () => {
      alive = false;
      clearTimeout(timer);
    };
  }, []);

  const go = async (fn: () => Promise<void>) => {
    try {
      await fn();
      kick.current();
    } catch (e) {
      setError(toApiError(e).message);
    }
  };

  return (
    <figure class="mirror">
      <div class="mirror__face">
        <canvas
          ref={canvas}
          class="mirror__canvas"
          width={W * PITCH}
          height={H * PITCH}
          role="img"
          aria-label={screen ? `The clock shows: ${LABELS[screen] ?? screen}` : 'The clock display'}
        />
      </div>
      <figcaption class="mirror__bar">
        <button type="button" class="btn btn--quiet mirror__nav" onClick={() => go(displayPrev)} aria-label="Previous screen">
          ‹ Previous
        </button>
        <span class="mirror__screen" aria-live="polite">
          {error ? <span class="mirror__error">{error}</span> : screen ? (LABELS[screen] ?? screen) : 'Connecting…'}
        </span>
        <button type="button" class="btn btn--quiet mirror__nav" onClick={() => go(displayNext)} aria-label="Next screen">
          Next ›
        </button>
      </figcaption>
    </figure>
  );
}

/** LEDs behind a diffuser: lit pixels glow from the centre, unlit ones stay faintly visible. */
function draw(c: HTMLCanvasElement, rgb: Uint8Array) {
  const ctx = c.getContext('2d');
  if (!ctx) return;
  const fill = PITCH * 0.84;
  const inset = (PITCH - fill) / 2;
  ctx.fillStyle = '#07090a';
  ctx.fillRect(0, 0, c.width, c.height);
  for (let i = 0; i < W * H; i++) {
    const r = rgb[i * 3];
    const g = rgb[i * 3 + 1];
    const b = rgb[i * 3 + 2];
    const x = (i % W) * PITCH + inset;
    const y = Math.floor(i / W) * PITCH + inset;
    if (r + g + b === 0) {
      ctx.fillStyle = '#101416';
    } else {
      const grad = ctx.createRadialGradient(x + fill / 2, y + fill / 2, 0, x + fill / 2, y + fill / 2, fill * 0.72);
      grad.addColorStop(0, `rgb(${r},${g},${b})`);
      grad.addColorStop(0.65, `rgb(${r},${g},${b})`);
      grad.addColorStop(1, `rgb(${Math.round(r * 0.78)},${Math.round(g * 0.78)},${Math.round(b * 0.78)})`);
      ctx.fillStyle = grad;
    }
    ctx.beginPath();
    ctx.roundRect(x, y, fill, fill, 4);
    ctx.fill();
  }
}
