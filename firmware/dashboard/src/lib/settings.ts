import { useEffect, useRef, useState } from 'preact/hooks';
import {
  getDisplaySettings,
  getSchedule,
  patchDisplaySettings,
  patchSchedule,
  toApiError,
} from '../api';
import type { DisplayPatch, DisplaySettings, SchedulePatch, ScheduleStatus } from '../contract';

export type SaveState = 'idle' | 'saving' | 'saved' | 'error';

export interface SettingsError {
  message: string;
  /** "group.key" when the clock named the field. */
  field?: string;
}

export interface Autosaved<T, P> {
  settings: T | null;
  loadError: string | null;
  saveState: SaveState;
  error: SettingsError | null;
  /** Apply locally at once, send to the clock after `delayMs` (changes batch up). */
  update: (patch: P, delayMs?: number) => void;
  reload: () => void;
}

interface Options<T, P> {
  load: () => Promise<T>;
  save: (patch: P) => Promise<T>;
  /** Apply a patch to a value, the way the clock will. */
  apply: (value: T, patch: P) => T;
  /** Combine two pending patches, later wins. */
  combine: (a: P, b: P) => P;
}

/**
 * Settings with autosave: every change shows at once and is PATCHed to the clock
 * shortly after. If the clock rejects it, the value goes back to what the clock
 * has and the error names the field.
 */
export function useAutosaved<T, P extends object>({ load, save, apply, combine }: Options<T, P>): Autosaved<T, P> {
  const [server, setServer] = useState<T | null>(null);
  const [local, setLocal] = useState<T | null>(null);
  const [loadError, setLoadError] = useState<string | null>(null);
  const [saveState, setSaveState] = useState<SaveState>('idle');
  const [error, setError] = useState<SettingsError | null>(null);

  const pending = useRef<P | null>(null);
  const timer = useRef<ReturnType<typeof setTimeout> | undefined>(undefined);
  const inFlight = useRef(false);
  const serverRef = useRef<T | null>(null);
  serverRef.current = server;

  const doLoad = async () => {
    setLoadError(null);
    try {
      const v = await load();
      setServer(v);
      if (!pending.current && !inFlight.current) setLocal(v);
    } catch (e) {
      setLoadError(toApiError(e).message);
    }
  };

  useEffect(() => {
    void doLoad();
    return () => clearTimeout(timer.current);
  }, []);

  const flush = async () => {
    if (inFlight.current || !pending.current) return;
    const patch = pending.current;
    pending.current = null;
    inFlight.current = true;
    setSaveState('saving');
    try {
      const saved = await save(patch);
      setServer(saved);
      // Keep edits made while this request was out.
      setLocal(pending.current ? apply(saved, pending.current) : saved);
      setSaveState(pending.current ? 'saving' : 'saved');
    } catch (e) {
      const a = toApiError(e);
      pending.current = null;
      setLocal(serverRef.current);
      setError({ message: a.message, field: a.field });
      setSaveState('error');
    } finally {
      inFlight.current = false;
      if (pending.current) void flush();
    }
  };

  const update = (patch: P, delayMs = 250) => {
    setLocal((prev) => (prev ? apply(prev, patch) : prev));
    pending.current = pending.current ? combine(pending.current, patch) : patch;
    setError(null);
    clearTimeout(timer.current);
    timer.current = setTimeout(() => void flush(), delayMs);
  };

  return { settings: local, loadError, saveState, error, update, reload: () => void doLoad() };
}

/** Merge one level deep: `{ group: { key } }`. */
function mergeGroups<T extends object>(a: T, b: object): T {
  const out = { ...a } as Record<string, object>;
  for (const [k, v] of Object.entries(b)) out[k] = { ...(out[k] ?? {}), ...v };
  return out as T;
}

export type DisplaySettingsHook = Autosaved<DisplaySettings, DisplayPatch>;

export const useDisplaySettings = (): DisplaySettingsHook =>
  useAutosaved<DisplaySettings, DisplayPatch>({
    load: getDisplaySettings,
    save: patchDisplaySettings,
    apply: mergeGroups,
    combine: mergeGroups,
  });

export type ScheduleHook = Autosaved<ScheduleStatus, SchedulePatch>;

const combineSchedule = (a: SchedulePatch, b: SchedulePatch): SchedulePatch => ({
  ...a,
  ...b,
  awake: a.awake || b.awake ? { ...a.awake, ...b.awake } : undefined,
});

export const useSchedule = (): ScheduleHook =>
  useAutosaved<ScheduleStatus, SchedulePatch>({
    load: getSchedule,
    save: patchSchedule,
    apply: (v, p) => ({
      ...v,
      settings: { ...v.settings, ...p, awake: { ...v.settings.awake, ...(p.awake ?? {}) } },
    }),
    combine: combineSchedule,
  });
