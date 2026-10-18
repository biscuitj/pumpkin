import { useRef, useState } from 'preact/hooks';
import { toApiError, uploadFirmware } from '../../api';
import type { DeviceInfo } from '../../contract';
import { checkImage, formatBytes, UPDATE_MESSAGES, type ImageProblem } from '../../lib/firmware';
import { Notice, type Tone } from '../../ui/Notice';
import type { RestartWith } from './index';

interface Props {
  dev: DeviceInfo;
  restartWith: RestartWith;
  setResult: (r: { tone: Tone; title: string; body?: string } | null) => void;
}

type Picked = { file: File; problem: ImageProblem | null };

export function Firmware({ dev, restartWith, setResult }: Props) {
  const input = useRef<HTMLInputElement>(null);
  const [picked, setPicked] = useState<Picked | null>(null);
  const [sent, setSent] = useState<number | null>(null);
  const [error, setError] = useState<string | null>(null);

  const pick = async (file: File | undefined) => {
    setError(null);
    setPicked(null);
    if (!file) return;
    setPicked({ file, problem: await checkImage(file) });
  };

  const install = () => {
    if (!picked || picked.problem) return;
    const file = picked.file;
    setSent(0);
    void restartWith(
      { title: 'Installing the update…', body: "Don't unplug the clock. It restarts by itself when it's done." },
      async () => {
        try {
          await uploadFirmware(file, (n) => setSent(n));
          return true;
        } catch (e) {
          const a = toApiError(e);
          // The clock names the reason in `field` (an UpdateError).
          const known = a.field && a.field in UPDATE_MESSAGES;
          setError(`${known ? UPDATE_MESSAGES[a.field as ImageProblem] : a.message} The clock keeps its current firmware.`);
          setSent(null);
          return false;
        }
      },
      (before, after) => {
        setSent(null);
        setPicked(null);
        if (input.current) input.current.value = '';
        const from = before?.firmware.version;
        setResult(
          from && after.firmware_version !== from
            ? { tone: 'ok', title: `Updated to ${after.firmware_version}.` }
            : { tone: 'warn', title: `The clock is back, still on ${after.firmware_version}.`, body: 'See the Firmware section for why.' },
        );
      },
    );
  };

  const rb = dev.firmware.rolled_back;

  return (
    <section class="section" aria-labelledby="fw-h">
      <div class="section__head">
        <h2 id="fw-h">Firmware</h2>
        <p class="section__lede">
          Pumpkin <strong>{dev.firmware.version}</strong> on {dev.firmware.hardware}.
        </p>
      </div>

      {rb ? (
        <Notice tone="warn" title={`The update to ${rb.from_version} didn't start properly, so the clock went back to ${dev.firmware.version}.`}>
          {rb.reason}
        </Notice>
      ) : null}

      <div class="field">
        <label class="field__label" for="fw-file">
          Install an update
        </label>
        <input
          ref={input}
          id="fw-file"
          class="file"
          type="file"
          accept=".bin,application/octet-stream"
          onChange={(e) => void pick(e.currentTarget.files?.[0])}
          aria-describedby="fw-file-msg"
        />
        <p class="field__msg" id="fw-file-msg">
          The firmware.bin from a Pumpkin release. The clock checks the file and keeps its current firmware if anything is
          wrong.
        </p>
      </div>

      {picked ? (
        picked.problem ? (
          <Notice tone="fail" title={UPDATE_MESSAGES[picked.problem]}>
            {picked.file.name}, {formatBytes(picked.file.size)}
          </Notice>
        ) : (
          <Notice tone="ok" title={`${picked.file.name} is Pumpkin firmware for this clock.`}>
            {formatBytes(picked.file.size)}
          </Notice>
        )
      ) : null}

      {sent !== null && picked ? (
        <div class="progress">
          <progress max={picked.file.size} value={sent} aria-label="Sending the update" />
          <span class="progress__text">
            {sent >= picked.file.size ? 'Checking and restarting…' : `Sending ${Math.floor((sent / picked.file.size) * 100)}%`}
          </span>
        </div>
      ) : null}

      {error ? <Notice tone="fail" title={error} live /> : null}

      {picked && !picked.problem ? (
        <div class="actions">
          <button type="button" class="btn btn--primary" onClick={install} disabled={sent !== null}>
            Install update
          </button>
        </div>
      ) : null}
    </section>
  );
}
