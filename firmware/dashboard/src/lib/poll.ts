import { useEffect, useRef, useState } from 'preact/hooks';
import { type ApiError, toApiError } from '../api';

export interface Poll<T> {
  data: T | null;
  error: ApiError | null;
  /** Fetch now instead of waiting for the next tick. */
  refresh: () => void;
}

/**
 * Poll an endpoint while the component is mounted and the tab is visible.
 * The interval can change between renders (e.g. faster during a Wi-Fi join).
 */
export function usePoll<T>(fetcher: () => Promise<T>, intervalMs: number): Poll<T> {
  const [data, setData] = useState<T | null>(null);
  const [error, setError] = useState<ApiError | null>(null);
  const interval = useRef(intervalMs);
  const fetcherRef = useRef(fetcher);
  const kick = useRef<() => void>(() => {});
  interval.current = intervalMs;
  fetcherRef.current = fetcher;

  useEffect(() => {
    let alive = true;
    let busy = false;
    let timer: ReturnType<typeof setTimeout> | undefined;

    const run = async () => {
      clearTimeout(timer);
      if (busy) return;
      if (!document.hidden) {
        busy = true;
        try {
          const d = await fetcherRef.current();
          if (alive) {
            setData(d);
            setError(null);
          }
        } catch (e) {
          if (alive) setError(toApiError(e));
        } finally {
          busy = false;
        }
      }
      if (alive) timer = setTimeout(run, interval.current);
    };

    kick.current = () => void run();
    void run();
    return () => {
      alive = false;
      clearTimeout(timer);
    };
  }, []);

  return { data, error, refresh: () => kick.current() };
}
