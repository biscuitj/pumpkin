import './device.css';
import { useEffect, useState } from 'preact/hooks';
import { factoryReset, getDevice, patchDevice, restartClock, setPassword, toApiError, waitForClock } from '../../api';
import type { DeviceInfo, Status } from '../../contract';
import { slug } from '../../lib/format';
import { usePoll } from '../../lib/poll';
import { browserZone, findZone, ZONE_GROUPS } from '../../lib/zones';
import { Field } from '../../ui/Field';
import { PasswordInput, TextInput } from '../../ui/inputs';
import { Notice, type Tone } from '../../ui/Notice';
import { Diagnostics } from './Diagnostics';
import { Firmware } from './Firmware';

export interface Busy {
  title: string;
  body?: string;
}

export type RestartWith = (
  busy: Busy,
  action: () => Promise<boolean | void>,
  done: (before: DeviceInfo | null, after: Status) => void,
) => Promise<void>;

export function DeviceTab() {
  const [diagOpen, setDiagOpen] = useState(false);
  const [busy, setBusy] = useState<Busy | null>(null);
  const [result, setResult] = useState<{ tone: Tone; title: string; body?: string } | null>(null);
  const dev = usePoll(getDevice, diagOpen ? 1000 : 10_000);
  const d = dev.data;

  /**
   * Run something that restarts the clock, then wait for it to come back.
   * `action` returns false when it handled a failure itself and nothing restarts.
   */
  const restartWith: RestartWith = async (busyMsg, action, done) => {
    const before = d;
    setResult(null);
    setBusy(busyMsg);
    try {
      if ((await action()) === false) return;
      const after = await waitForClock();
      dev.refresh();
      done(before, after);
    } catch (e) {
      setResult({ tone: 'fail', title: toApiError(e).message });
    } finally {
      setBusy(null);
    }
  };

  if (!d) {
    return dev.error ? <Notice tone="fail" title={dev.error.message} /> : <p class="muted">Asking the clock…</p>;
  }

  return (
    <div class="tab">
      {busy ? (
        <Notice tone="idle" title={busy.title} live>
          {busy.body}
        </Notice>
      ) : result ? (
        <Notice tone={result.tone} title={result.title} live>
          {result.body}
        </Notice>
      ) : null}

      <fieldset class="device-body" disabled={!!busy}>
        <NameSection dev={d} onSaved={dev.refresh} />
        <TimeSection dev={d} onSaved={dev.refresh} />
        <PasswordSection dev={d} onSaved={dev.refresh} />
        <Firmware dev={d} restartWith={restartWith} setResult={setResult} />

        <section class="section" aria-labelledby="restart-h">
          <div class="section__head">
            <h2 id="restart-h">Restart and reset</h2>
          </div>
          <div class="actions">
            <button
              type="button"
              class="btn"
              onClick={() =>
                restartWith({ title: 'Restarting…', body: 'This takes about 20 seconds.' }, restartClock, () =>
                  setResult({ tone: 'ok', title: 'The clock restarted and is back.' }),
                )
              }
            >
              Restart the clock
            </button>
          </div>
          <FactoryReset dev={d} onErased={() => setResult(erasedMessage(d))} setBusy={setBusy} />
        </section>

        <Diagnostics dev={d} open={diagOpen} onToggle={setDiagOpen} />
      </fieldset>
    </div>
  );
}

const erasedMessage = (d: DeviceInfo) => ({
  tone: 'idle' as Tone,
  title: 'The clock is erased and restarting into its setup hotspot.',
  body: `Join the Wi-Fi network that starts with Pumpkin-Setup on your phone to set it up again. ${d.local_name ?? 'Its old address'} won't answer any more.`,
});

// ---------------------------------------------------------------- name

function NameSection({ dev, onSaved }: { dev: DeviceInfo; onSaved: () => void }) {
  const [name, setName] = useState(dev.name);
  const [error, setError] = useState<string | null>(null);
  const [saving, setSaving] = useState(false);
  const [moving, setMoving] = useState<string | null>(null);
  useEffect(() => setName(dev.name), [dev.name]);

  const trimmed = name.trim();
  const changed = trimmed !== dev.name;
  const nextLocal = `${slug(trimmed)}.local`;

  const save = async (e: Event) => {
    e.preventDefault();
    if (!trimmed) return setError('Enter a name.');
    if (trimmed.length > 32) return setError('Names are 32 characters at most.');
    setSaving(true);
    try {
      const saved = await patchDevice({ name: trimmed });
      onSaved();
      // Opened through the old .local name? Follow the clock to its new one.
      if (location.hostname === dev.local_name && saved.local_name && saved.local_name !== dev.local_name) {
        setMoving(saved.local_name);
        setTimeout(() => location.assign(`http://${saved.local_name}/#/device`), 3000);
      }
    } catch (err) {
      setError(toApiError(err).message);
    } finally {
      setSaving(false);
    }
  };

  return (
    <section class="section" aria-labelledby="name-h">
      <div class="section__head">
        <h2 id="name-h">Name</h2>
        <p class="section__lede">Tells your clocks apart, and gives this one its address on the network.</p>
      </div>
      <form class="form" onSubmit={save} noValidate>
        <Field
          id="clock-name"
          label="Clock name"
          error={error ?? undefined}
          hint={
            changed && trimmed ? (
              <>
                The address becomes <strong>{nextLocal}</strong>. Bookmarks to the old one stop working.
              </>
            ) : dev.local_name ? (
              <>
                Address: <a href={`http://${dev.local_name}`}>{dev.local_name}</a>
              </>
            ) : undefined
          }
        >
          <TextInput
            id="clock-name"
            value={name}
            onInput={(v) => {
              setName(v);
              setError(null);
            }}
            maxLength={32}
            invalid={!!error}
          />
        </Field>
        {moving ? <Notice tone="idle" title={`Moving to ${moving}…`} live /> : null}
        {changed ? (
          <div class="actions">
            <button type="submit" class="btn btn--primary" disabled={saving}>
              {saving ? 'Saving…' : 'Save name'}
            </button>
            <button type="button" class="btn btn--quiet" onClick={() => setName(dev.name)}>
              Cancel
            </button>
          </div>
        ) : null}
      </form>
    </section>
  );
}

// ---------------------------------------------------------------- time

const SOURCE: Record<DeviceInfo['time']['source'], string> = {
  ntp: 'from the internet',
  http: "from the bridge's server (this network blocks internet time)",
  rtc: "from the clock's own battery clock (no internet time yet)",
  none: '',
};

function TimeSection({ dev, onSaved }: { dev: DeviceInfo; onSaved: () => void }) {
  const [error, setError] = useState<string | null>(null);
  const [zone, setZone] = useState(dev.time.zone);
  useEffect(() => setZone(dev.time.zone), [dev.time.zone]);
  const mine = browserZone();
  const known = findZone(zone);

  const choose = async (id: string) => {
    const z = findZone(id);
    if (!z) return;
    setZone(id);
    setError(null);
    try {
      await patchDevice({ time_zone: { zone: z.id, posix: z.posix } });
      onSaved();
    } catch (e) {
      setError(toApiError(e).message);
      setZone(dev.time.zone);
    }
  };

  const local = dev.time.local ? new Date(dev.time.local) : null;
  const shown = local
    ? local.toLocaleString(undefined, { weekday: 'long', day: 'numeric', month: 'long', hour: '2-digit', minute: '2-digit' })
    : null;

  return (
    <section class="section" aria-labelledby="time-h">
      <div class="section__head">
        <h2 id="time-h">Time</h2>
      </div>
      {shown ? (
        <p>
          The clock says <strong>{shown}</strong>, {SOURCE[dev.time.source]}.
        </p>
      ) : (
        <Notice tone="warn" title="The clock doesn't know the time yet.">
          It gets it from the internet once it's online. Check time sync on the Connections tab.
        </Notice>
      )}
      <Field id="zone" label="Time zone" error={error ?? undefined}>
        <select id="zone" class="input" value={zone} onChange={(e) => void choose(e.currentTarget.value)}>
          {!known ? <option value={zone}>{zone}</option> : null}
          {ZONE_GROUPS.map((g) => (
            <optgroup key={g.name} label={g.name}>
              {g.zones.map((z) => (
                <option key={z.id} value={z.id}>
                  {z.label}
                </option>
              ))}
            </optgroup>
          ))}
        </select>
      </Field>
      {mine && mine.id !== zone ? (
        <div class="actions actions--tight">
          <button type="button" class="btn btn--small" onClick={() => void choose(mine.id)}>
            Use this device's time zone ({mine.label})
          </button>
        </div>
      ) : null}
    </section>
  );
}

// ---------------------------------------------------------------- password

function PasswordSection({ dev, onSaved }: { dev: DeviceInfo; onSaved: () => void }) {
  const [pw, setPw] = useState('');
  const [error, setError] = useState<string | null>(null);
  const [done, setDone] = useState<string | null>(null);
  const [saving, setSaving] = useState(false);

  const save = async (value: string) => {
    if (value && (value.length < 4 || value.length > 64)) return setError('Use 4 to 64 characters.');
    setSaving(true);
    try {
      await setPassword(value);
      setPw('');
      setDone(value ? 'Password set. Your browser asks for it next time: the user name is pumpkin.' : 'Password removed.');
      onSaved();
    } catch (e) {
      setError(toApiError(e).message);
    } finally {
      setSaving(false);
    }
  };

  return (
    <section class="section" aria-labelledby="pw-h">
      <div class="section__head">
        <h2 id="pw-h">Password</h2>
        <p class="section__lede">
          {dev.password_set
            ? 'This page asks for a password.'
            : 'No password: anyone on this network can open this page and change the clock.'}
        </p>
      </div>
      <form
        class="form"
        noValidate
        onSubmit={(e) => {
          e.preventDefault();
          void save(pw);
        }}
      >
        <Field
          id="new-password"
          label={dev.password_set ? 'New password' : 'Password'}
          error={error ?? undefined}
          hint="The user name is pumpkin. The page isn't encrypted, so this keeps curious people out on a shared network; don't reuse an important password."
        >
          <PasswordInput
            id="new-password"
            value={pw}
            onInput={(v) => {
              setPw(v);
              setError(null);
              setDone(null);
            }}
            maxLength={64}
            invalid={!!error}
            autoComplete="new-password"
          />
        </Field>
        {done ? <Notice tone="ok" title={done} live /> : null}
        <div class="actions">
          <button type="submit" class="btn btn--primary" disabled={!pw || saving}>
            {dev.password_set ? 'Change password' : 'Set password'}
          </button>
          {dev.password_set ? (
            <button type="button" class="btn btn--quiet" onClick={() => void save('')} disabled={saving}>
              Remove password
            </button>
          ) : null}
        </div>
      </form>
    </section>
  );
}

// ---------------------------------------------------------------- reset

function FactoryReset({ dev, onErased, setBusy }: { dev: DeviceInfo; onErased: () => void; setBusy: (b: Busy | null) => void }) {
  const [asking, setAsking] = useState(false);
  const [error, setError] = useState<string | null>(null);

  const erase = async () => {
    setBusy({ title: 'Erasing…' });
    try {
      await factoryReset();
      onErased();
      setAsking(false);
    } catch (e) {
      setError(toApiError(e).message);
    } finally {
      setBusy(null);
    }
  };

  if (!asking) {
    return (
      <div class="actions">
        <button type="button" class="btn btn--danger-quiet" onClick={() => setAsking(true)}>
          Erase everything…
        </button>
      </div>
    );
  }
  return (
    <div class="confirm" role="group" aria-labelledby="erase-h">
      <p id="erase-h" class="confirm__title">
        Erase {dev.name}?
      </p>
      <p class="muted">
        This removes the Wi-Fi details, the bridge address and token, the password and every setting. The clock restarts
        into its setup hotspot, as if new.
      </p>
      {error ? <p class="field__msg field__msg--error">{error}</p> : null}
      <div class="actions">
        <button type="button" class="btn btn--danger" onClick={() => void erase()}>
          Erase and restart
        </button>
        <button type="button" class="btn btn--quiet" onClick={() => setAsking(false)}>
          Keep everything
        </button>
      </div>
    </div>
  );
}
