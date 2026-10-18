import { computed, signal } from '@preact/signals';
import { getStatus } from './api';
import type { Status } from './contract';

export const status = signal<Status | null>(null);
export const lastError = signal<string | null>(null);

export type Connection = 'connecting' | 'online' | 'offline';
export const connection = computed<Connection>(() => {
  if (lastError.value) return 'offline';
  return status.value ? 'online' : 'connecting';
});

const POLL_MS = 5000;
let started = false;

async function poll(): Promise<void> {
  // A phone left on the dashboard shouldn't keep the ESP32 busy.
  if (!document.hidden) {
    try {
      status.value = await getStatus();
      lastError.value = null;
    } catch (e) {
      lastError.value = e instanceof Error ? e.message : String(e);
    }
  }
  setTimeout(poll, POLL_MS);
}

export function startPolling(): void {
  if (started) return;
  started = true;
  void poll();
}
