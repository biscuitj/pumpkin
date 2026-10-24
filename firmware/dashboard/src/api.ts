import type {
  AlertsClear,
  ApiErrorBody,
  PetSprites,
  DeviceInfo,
  DevicePatch,
  SchedulePatch,
  ScheduleStatus,
  UpdateStatus,
  DisplayPatch,
  DisplayPreview,
  DisplaySettings,
  SourceStatus,
  SourceTest,
  SourceTestRequest,
  SourceUpdate,
  Status,
  WifiJoin,
  WifiScan,
  WifiStatus,
} from './contract';

export class ApiError extends Error {
  status: number;
  field?: string;
  constructor(message: string, status: number, field?: string) {
    super(message);
    this.status = status;
    this.field = field;
  }
}

const TIMEOUT_MS = 6000;

async function request<T>(method: string, path: string, body?: unknown, type = 'application/json'): Promise<T> {
  const ctrl = new AbortController();
  const timer = setTimeout(() => ctrl.abort(), TIMEOUT_MS);
  const headers: Record<string, string> = { Accept: 'application/json' };
  if (body !== undefined) headers['Content-Type'] = type;

  try {
    let res: Response;
    try {
      res = await fetch(path, {
        method,
        headers,
        signal: ctrl.signal,
        body: body === undefined ? undefined : typeof body === 'string' ? body : JSON.stringify(body),
      });
    } catch {
      throw new ApiError(
        ctrl.signal.aborted ? 'The clock took too long to answer.' : "Can't reach the clock.",
        0,
      );
    }

    const text = await res.text();
    let data: unknown = null;
    if (text) {
      try {
        data = JSON.parse(text);
      } catch {
        data = null;
      }
    }

    if (!res.ok) {
      const err = (data ?? {}) as Partial<ApiErrorBody>;
      throw new ApiError(err.error ?? `The clock answered ${res.status}.`, res.status, err.field);
    }
    return data as T;
  } finally {
    clearTimeout(timer);
  }
}

export const toApiError = (e: unknown): ApiError =>
  e instanceof ApiError ? e : new ApiError(e instanceof Error ? e.message : String(e), 0);

export const getStatus = () => request<Status>('GET', '/api/status');

export const getWifi = () => request<WifiStatus>('GET', '/api/wifi');
export const getWifiScan = () => request<WifiScan>('GET', '/api/wifi/networks');
export const startWifiScan = () => request<void>('POST', '/api/wifi/networks/scan');
export const joinWifi = (body: WifiJoin) => request<void>('POST', '/api/wifi/join', body);
export const recheckNetwork = () => request<void>('POST', '/api/wifi/checks');
export const uploadCa = (pem: string) => request<void>('PUT', '/api/wifi/ca', pem, 'text/plain');
export const removeCa = () => request<void>('DELETE', '/api/wifi/ca');

export const getSource = () => request<SourceStatus>('GET', '/api/source');
export const saveSource = (body: SourceUpdate) => request<SourceStatus>('PUT', '/api/source', body);
export const startSourceTest = (body: SourceTestRequest) => request<void>('POST', '/api/source/test', body);
export const getSourceTest = () => request<SourceTest>('GET', '/api/source/test');

export const getDisplaySettings = () => request<DisplaySettings>('GET', '/api/display/settings');
export const patchDisplaySettings = (patch: DisplayPatch) =>
  request<DisplaySettings>('PATCH', '/api/display/settings', patch);
export const displayNext = () => request<void>('POST', '/api/display/next');
export const displayPrev = () => request<void>('POST', '/api/display/prev');
export const previewOnClock = (body: DisplayPreview) => request<void>('POST', '/api/display/preview', body);
export const getPetSprites = () => request<PetSprites>('GET', '/api/display/pets');

export interface Frame {
  /** null when unchanged (304) or not ready yet (204). */
  rgb: Uint8Array | null;
  screen: string | null;
  etag: string | null;
  /** 'low' while the low lock holds the glucose screen. */
  hold: string | null;
}

/** One mirror frame, or only its headers when nothing changed. */
export async function getFrame(etag: string | null): Promise<Frame> {
  const res = await fetch('/api/display/frame', {
    headers: etag ? { 'If-None-Match': etag } : {},
    cache: 'no-store',
  });
  const hold = res.headers.get('X-Hold');
  if (res.status === 304 || res.status === 204) return { rgb: null, screen: null, etag, hold };
  if (!res.ok) throw new ApiError(`The clock answered ${res.status}.`, res.status);
  const buf = new Uint8Array(await res.arrayBuffer());
  if (buf.length !== 768) throw new ApiError('Unexpected frame size.', res.status);
  return { rgb: buf, screen: res.headers.get('X-Screen'), etag: res.headers.get('ETag'), hold };
}

export const clearAlerts = () => request<void>('POST', '/api/alerts/clear');
export const getAlertsClear = () => request<AlertsClear>('GET', '/api/alerts/clear');

export const getSchedule = () => request<ScheduleStatus>('GET', '/api/schedule');
export const patchSchedule = (patch: SchedulePatch) => request<ScheduleStatus>('PATCH', '/api/schedule', patch);

export const getDevice = () => request<DeviceInfo>('GET', '/api/device');
export const patchDevice = (patch: DevicePatch) => request<DeviceInfo>('PATCH', '/api/device', patch);
export const setPassword = (password: string) => request<void>('PUT', '/api/device/password', { password });
export const restartClock = () => request<void>('POST', '/api/device/restart');
export const factoryReset = () => request<void>('POST', '/api/device/factory-reset', { confirm: 'erase' });
export const getUpdateStatus = () => request<UpdateStatus>('GET', '/api/update');

/**
 * Upload a firmware image with progress. XHR rather than fetch: fetch has no
 * upload progress. Resolves when the clock has accepted the image.
 */
export function uploadFirmware(file: Blob, onProgress: (sent: number, total: number) => void): Promise<void> {
  return new Promise((resolve, reject) => {
    const xhr = new XMLHttpRequest();
    xhr.open('POST', '/api/update');
    xhr.setRequestHeader('Content-Type', 'application/octet-stream');
    xhr.upload.onprogress = (e) => onProgress(e.loaded, e.total || file.size);
    xhr.onload = () => {
      if (xhr.status >= 200 && xhr.status < 300) return resolve();
      let body: Partial<ApiErrorBody> = {};
      try {
        body = JSON.parse(xhr.responseText);
      } catch {
        // not JSON
      }
      reject(new ApiError(body.error ?? `The clock answered ${xhr.status}.`, xhr.status, body.field));
    };
    xhr.onerror = () => reject(new ApiError("The upload broke off. The clock keeps its current firmware.", 0));
    xhr.send(file);
  });
}

/** Wait for the clock to answer again after a restart. Resolves with its status. */
export async function waitForClock(timeoutMs = 90_000): Promise<Status> {
  const until = Date.now() + timeoutMs;
  // Give it a moment to actually go down first.
  await new Promise((r) => setTimeout(r, 2500));
  while (Date.now() < until) {
    try {
      return await getStatus();
    } catch {
      await new Promise((r) => setTimeout(r, 1500));
    }
  }
  throw new ApiError("The clock hasn't come back. Check that it's powered and on Wi-Fi.", 0);
}
