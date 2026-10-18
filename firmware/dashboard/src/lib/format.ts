export function age(sec: number | null): string {
  if (sec === null) return 'unknown';
  if (sec < 60) return 'under a minute';
  const min = Math.floor(sec / 60);
  if (min < 60) return `${min} min`;
  const h = Math.floor(min / 60);
  const rest = min % 60;
  return rest ? `${h} h ${rest} min` : `${h} h`;
}

export function glucose(mgdl: number | null, mmol: boolean): string {
  if (mgdl === null) return '';
  return mmol ? `${(mgdl / 18.016).toFixed(1)} mmol/L` : `${mgdl} mg/dL`;
}

export function hostOf(url: string): string | null {
  try {
    return new URL(url).host;
  } catch {
    return null;
  }
}

/** Wi-Fi limits are in bytes, not characters. */
export const byteLength = (s: string) => new TextEncoder().encode(s).length;

/**
 * The `.local` name the clock derives from its display name, as the firmware
 * does it (src/core/identity.cpp): accents folded, lowercase, dashes, 32 at most.
 * The clock may add "-2" if the name is taken on the network.
 */
const FOLD: Record<string, string> = { ß: 'ss', æ: 'ae', Æ: 'ae', ø: 'o', Ø: 'o', þ: 'th', Þ: 'th', ð: 'd', Ð: 'd' };

export function slug(name: string): string {
  const folded = [...name].map((c) => FOLD[c] ?? c).join('').normalize('NFD').replace(/[\u0300-\u036f]/g, '');
  const joined = folded.toLowerCase().split(/[^a-z0-9]+/).filter(Boolean).join('-');
  const out = joined.slice(0, 32).replace(/-+$/, '');
  return out || 'pumpkin';
}
