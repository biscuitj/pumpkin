import type { CheckResult, SourceReport, SourceStatus, WifiStatus } from '../../contract';
import { age, hostOf } from '../../lib/format';
import type { Tone } from '../../ui/Notice';

export interface Hop {
  label: string;
  tone: Tone;
  detail: string;
}

/**
 * The chain a reading travels, clock-side first. Once a hop fails, the hops
 * after it are unknown: there's no point blaming CareLink when Wi-Fi is down.
 */
export function connectionHops(wifi: WifiStatus | null, source: SourceStatus | null): Hop[] {
  const hops: Hop[] = [];
  const unknown = (label: string, detail = 'Waiting'): Hop => ({ label, tone: 'idle', detail });

  // Wi-Fi
  if (!wifi) hops.push(unknown('Wi-Fi', 'Checking'));
  else if (wifi.mode === 'connected') hops.push({ label: 'Wi-Fi', tone: 'ok', detail: wifi.ssid ?? 'Connected' });
  else if (wifi.mode === 'setup_ap') hops.push({ label: 'Wi-Fi', tone: 'warn', detail: 'Setup hotspot' });
  else if (wifi.mode === 'connecting') hops.push(unknown('Wi-Fi', 'Connecting'));
  else hops.push({ label: 'Wi-Fi', tone: 'fail', detail: 'Not connected' });

  // Internet
  const c = wifi?.checks;
  if (hops[0].tone !== 'ok' || !c) hops.push(unknown('Internet'));
  else if (c.running) hops.push(unknown('Internet', 'Checking'));
  else if (c.dns === 'fail' || c.data === 'fail') hops.push({ label: 'Internet', tone: 'fail', detail: 'Blocked' });
  else if (c.ntp === 'fail') hops.push({ label: 'Internet', tone: 'warn', detail: 'No time sync' });
  else hops.push({ label: 'Internet', tone: 'ok', detail: 'Open' });

  // Bridge, then CareLink
  if (source?.state === 'demo') {
    hops.push({ label: 'Bridge', tone: 'idle', detail: 'Demo data' }, { label: 'CareLink', tone: 'idle', detail: 'Demo data' });
    return hops;
  }
  if (hops[1].tone === 'fail' || hops[1].tone === 'idle' || !source) {
    hops.push(unknown('Bridge'), unknown('CareLink'));
    return hops;
  }

  switch (source.state) {
    case 'not_configured':
      hops.push({ label: 'Bridge', tone: 'idle', detail: 'Not set up' }, unknown('CareLink'));
      return hops;
    case 'waiting':
      hops.push(unknown('Bridge', 'Asking'), unknown('CareLink'));
      return hops;
    case 'unreachable':
      hops.push({ label: 'Bridge', tone: 'fail', detail: 'Unreachable' }, unknown('CareLink'));
      return hops;
    case 'unauthorized':
      hops.push({ label: 'Bridge', tone: 'fail', detail: 'Token refused' }, unknown('CareLink'));
      return hops;
  }

  hops.push({ label: 'Bridge', tone: 'ok', detail: 'Answering' });
  switch (source.state) {
    case 'login_needed':
      hops.push({ label: 'CareLink', tone: 'fail', detail: 'Login needed' });
      break;
    case 'carelink_error':
      hops.push({ label: 'CareLink', tone: 'fail', detail: 'Failing' });
      break;
    case 'stale':
      hops.push({ label: 'CareLink', tone: 'warn', detail: `${age(source.reading_age_sec)} old` });
      break;
    default:
      hops.push(
        source.upstream === 'nightscout'
          ? { label: 'CareLink', tone: 'warn', detail: 'Via Nightscout' }
          : { label: 'CareLink', tone: 'ok', detail: `${age(source.reading_age_sec)} old` },
      );
  }
  return hops;
}

export interface Message {
  tone: Tone;
  title: string;
  body?: string;
}

/** Plain-language explanation of one bridge fetch, with what to do about it. */
export function describeReport(r: SourceReport, bridgeUrl: string, reading: string): Message {
  const host = hostOf(bridgeUrl) ?? 'the bridge';
  switch (r.state) {
    case 'ok':
      return r.upstream === 'nightscout'
        ? {
            tone: 'warn',
            title: `Readings come from Nightscout. The latest is ${age(r.reading_age_sec)} old.`,
            body: "The bridge can't reach CareLink and is using Nightscout as a fallback. Check the bridge's /debug page.",
          }
        : {
            tone: 'ok',
            title: `Readings are coming in. The latest is ${age(r.reading_age_sec)} old${reading ? `: ${reading}` : ''}.`,
          };
    case 'stale':
      return {
        tone: 'warn',
        title: `The latest reading is ${age(r.reading_age_sec)} old.`,
        body: 'The bridge answers, but CareLink has nothing newer. Check that the pump is connected to its phone and the phone is online.',
      };
    case 'login_needed':
      return {
        tone: 'fail',
        title: 'CareLink signed the bridge out.',
        body: 'Run npm run login in the bridge folder to sign in again. Readings come back by themselves after that.',
      };
    case 'carelink_error':
      return {
        tone: 'fail',
        title: 'CareLink keeps failing.',
        body: "The bridge retries by itself. If this lasts, open the bridge's /debug page to see why.",
      };
    case 'unauthorized':
      return {
        tone: 'fail',
        title: 'The bridge refused the clock token.',
        body: 'Copy the CLOCK_TOKEN secret from the bridge into Clock token below.',
      };
    case 'unreachable':
      return {
        tone: 'fail',
        title: "The clock can't reach the bridge.",
        body: `${r.error ? `${r.error}. ` : ''}Check the address. On a school network, ask IT to allow HTTPS to ${host}.`,
      };
    case 'not_configured':
      return { tone: 'idle', title: 'Not set up yet.', body: "Enter your bridge's address and clock token below." };
    case 'waiting':
      return { tone: 'idle', title: 'Waiting for the first answer from the bridge.' };
    case 'demo':
      return {
        tone: 'idle',
        title: 'The clock is showing demo data.',
        body: 'Switch to Pumpkin bridge to show real readings.',
      };
  }
}

export const CHECK_LABEL: Record<CheckResult, string> = {
  pending: 'Checking',
  ok: 'Works',
  fail: 'Blocked',
  skipped: 'Not checked',
};
