import { useEffect, useRef, useState } from 'preact/hooks';
import { getWifiScan, joinWifi, removeCa, startWifiScan, toApiError, uploadCa } from '../../api';
import type { EapMethod, WifiJoin as JoinBody, WifiNetwork, WifiSecurity, WifiStatus } from '../../contract';
import { byteLength } from '../../lib/format';
import { usePoll } from '../../lib/poll';
import { Field } from '../../ui/Field';
import { Lock, SignalBars } from '../../ui/icons';
import { PasswordInput, Segmented, TextInput } from '../../ui/inputs';
import { Notice, type Tone } from '../../ui/Notice';

const SECURITY = [
  { value: 'personal', label: 'Password' },
  { value: 'enterprise', label: 'School or work login' },
] as const;

const EAP = [
  { value: 'peap', label: 'PEAP' },
  { value: 'ttls', label: 'EAP-TTLS' },
] as const;

type Errors = Partial<Record<'ssid' | 'password' | 'identity' | 'validate_ca' | 'ca' | 'form', string>>;

interface Props {
  wifi: WifiStatus;
  onStarted: () => void;
  onCancel?: () => void;
}

export function WifiJoin({ wifi, onStarted, onCancel }: Props) {
  const scan = useScan();
  const [ssid, setSsid] = useState('');
  const [security, setSecurity] = useState<WifiSecurity>('personal');
  const [password, setPassword] = useState('');
  const [identity, setIdentity] = useState('');
  const [eap, setEap] = useState<EapMethod>('peap');
  const [anon, setAnon] = useState('');
  const [validateCa, setValidateCa] = useState(wifi.ca.validate);
  const [pem, setPem] = useState('');
  const [errors, setErrors] = useState<Errors>({});
  const [submitting, setSubmitting] = useState(false);
  const [submitted, setSubmitted] = useState<{ ssid: string; security: WifiSecurity } | null>(null);

  const edit = <T,>(set: (v: T) => void) => (v: T) => {
    set(v);
    setErrors({});
  };

  const pick = (n: WifiNetwork) => {
    setSsid(n.ssid);
    setSecurity(n.enterprise ? 'enterprise' : 'personal');
    setErrors({});
  };

  const trial = wifi.trial;
  const trying = trial.state === 'associating' || trial.state === 'authenticating';
  const progress = submitted && trial.ssid === submitted.ssid ? trialMessage(wifi, submitted.security) : null;
  const moving = wifi.mode === 'connected' && ssid && ssid !== wifi.ssid;

  const validate = (): Errors => {
    const e: Errors = {};
    const n = byteLength(ssid);
    if (!n) e.ssid = 'Enter the network name, or pick one above.';
    else if (n > 32) e.ssid = 'Network names are 32 bytes at most.';
    if (security === 'personal') {
      if (password && (password.length < 8 || password.length > 63))
        e.password = 'Wi-Fi passwords are 8 to 63 characters.';
    } else {
      if (!identity) e.identity = 'Enter the username.';
      if (!password) e.password = 'Enter the password.';
      if (validateCa && !wifi.ca.installed) e.validate_ca = 'Upload the certificate first.';
    }
    return e;
  };

  const join = async (ev: Event) => {
    ev.preventDefault();
    const e = validate();
    setErrors(e);
    if (Object.keys(e).length) return;

    const body: JoinBody =
      security === 'personal'
        ? { ssid, security, password }
        : { ssid, security, eap, identity, password, anon_identity: anon, validate_ca: validateCa };

    setSubmitting(true);
    try {
      await joinWifi(body);
      setSubmitted({ ssid, security });
      setPassword('');
      onStarted();
    } catch (err) {
      const a = toApiError(err);
      setErrors(a.field ? { [a.field]: a.message } : { form: a.message });
    } finally {
      setSubmitting(false);
    }
  };

  const saveCa = async () => {
    const text = pem.trim();
    if (!text.includes('-----BEGIN CERTIFICATE-----') || !text.includes('-----END CERTIFICATE-----')) {
      setErrors({ ca: 'Paste the whole certificate, from -----BEGIN CERTIFICATE----- to the END line.' });
      return;
    }
    try {
      await uploadCa(text);
      setPem('');
      setValidateCa(true);
      onStarted();
    } catch (err) {
      setErrors({ ca: toApiError(err).message });
    }
  };

  const dropCa = async () => {
    try {
      await removeCa();
      setValidateCa(false);
      onStarted();
    } catch (err) {
      setErrors({ ca: toApiError(err).message });
    }
  };

  return (
    <form class="join" onSubmit={join} noValidate>
      <div class="join__head">
        <h3>Join a network</h3>
        <button type="button" class="btn btn--quiet" onClick={scan.start} disabled={scan.scanning}>
          {scan.scanning ? 'Scanning…' : 'Scan again'}
        </button>
      </div>
      <p class="muted">Scanning takes a few seconds and can briefly interrupt this page.</p>

      <ul class="networks" aria-label="Networks the clock can see" aria-busy={scan.scanning}>
        {scan.networks.length === 0 ? (
          <li class="networks__empty muted">{scan.scanning ? 'Looking for networks…' : 'No networks found yet.'}</li>
        ) : (
          scan.networks.map((n) => (
            <li key={n.ssid}>
              <button
                type="button"
                class="networks__item"
                aria-pressed={n.ssid === ssid}
                onClick={() => pick(n)}
              >
                <SignalBars rssi={n.rssi} />
                <span class="networks__name">{n.ssid}</span>
                <span class="networks__kind">
                  {n.enterprise ? 'School or work login' : n.secure ? <Lock /> : 'Open'}
                </span>
              </button>
            </li>
          ))
        )}
      </ul>

      <Field
          id="wifi-ssid"
          label="Network name"
          error={errors.ssid}
          hint="Pick one above, or type the name of a hidden network."
        >
        <TextInput id="wifi-ssid" value={ssid} onInput={edit(setSsid)} invalid={!!errors.ssid} />
      </Field>

      <Segmented name="wifi-security" label="How it signs in" value={security} options={SECURITY} onChange={edit(setSecurity)} />

      {security === 'personal' ? (
        <Field id="wifi-password" label="Password" error={errors.password} hint="Leave empty for an open network.">
          <PasswordInput id="wifi-password" value={password} onInput={edit(setPassword)} maxLength={63} invalid={!!errors.password} />
        </Field>
      ) : (
        <>
          <Field
            id="wifi-identity"
            label="Username"
            error={errors.identity}
            hint="Exactly as IT gave it: user@school.be, SCHOOL\user or just user."
          >
            <TextInput id="wifi-identity" value={identity} onInput={edit(setIdentity)} maxLength={127} invalid={!!errors.identity} />
          </Field>
          <Field id="wifi-eap-password" label="Password" error={errors.password}>
            <PasswordInput id="wifi-eap-password" value={password} onInput={edit(setPassword)} maxLength={127} invalid={!!errors.password} />
          </Field>
          <Segmented name="wifi-eap" label="Method (almost always PEAP)" value={eap} options={EAP} onChange={edit(setEap)} />

          <details class="advanced">
            <summary>Advanced</summary>
            <Field id="wifi-anon" label="Anonymous identity" hint="Leave empty unless IT told you to set one.">
              <TextInput id="wifi-anon" value={anon} onInput={edit(setAnon)} maxLength={127} />
            </Field>

            <div class="field">
              <span class="field__label">Server certificate</span>
              {wifi.ca.installed ? (
                <div class="saved-secret">
                  <span>A certificate is saved on the clock</span>
                  <button type="button" class="btn btn--quiet" onClick={dropCa}>
                    Remove
                  </button>
                </div>
              ) : (
                <>
                  <textarea
                    id="wifi-ca"
                    class="input input--mono"
                    rows={4}
                    value={pem}
                    onInput={(e) => edit(setPem)(e.currentTarget.value)}
                    placeholder="-----BEGIN CERTIFICATE-----"
                    spellcheck={false}
                    aria-describedby="wifi-ca-msg"
                  />
                  <div class="actions actions--tight">
                    <button type="button" class="btn btn--quiet" onClick={saveCa} disabled={!pem.trim()}>
                      Upload certificate
                    </button>
                  </div>
                </>
              )}
              <p class={errors.ca ? 'field__msg field__msg--error' : 'field__msg'} id="wifi-ca-msg">
                {errors.ca ??
                  "Optional. Without one, the clock trusts whatever server the network presents, like a phone when you tap Trust."}
              </p>
            </div>

            <label class="check-row">
              <input
                type="checkbox"
                checked={validateCa}
                disabled={!wifi.ca.installed}
                onChange={(e) => edit(setValidateCa)(e.currentTarget.checked)}
              />
              <span>Check the network's certificate before sending the password</span>
            </label>
            {errors.validate_ca ? <p class="field__msg field__msg--error">{errors.validate_ca}</p> : null}
          </details>
        </>
      )}

      {progress ? (
        <Notice tone={progress.tone} title={progress.title} live>
          {progress.body}
        </Notice>
      ) : null}
      {errors.form ? <Notice tone="fail" title={errors.form} live /> : null}

      <div class="actions">
        <button type="submit" class="btn btn--primary" disabled={submitting || trying}>
          {trying ? 'Joining…' : 'Join network'}
        </button>
        {onCancel ? (
          <button type="button" class="btn btn--quiet" onClick={onCancel}>
            Cancel
          </button>
        ) : null}
      </div>
      <p class="muted">
        {moving && wifi.local_name
          ? `The clock tries ${ssid} first and keeps the current network if that fails. If it works, the clock moves and this page stops updating: join ${ssid} yourself and open ${wifi.local_name}.`
          : 'The clock tries the network first and only saves it once the join works.'}
      </p>
    </form>
  );
}

function trialMessage(wifi: WifiStatus, security: WifiSecurity): { tone: Tone; title: string; body?: string } | null {
  const { state, ssid, detail } = wifi.trial;
  const name = ssid ?? 'the network';
  const extra = detail ?? undefined;
  switch (state) {
    case 'associating':
      return { tone: 'idle', title: `Looking for ${name}…` };
    case 'authenticating':
      return { tone: 'idle', title: `Signing in to ${name}…` };
    case 'connected':
      return {
        tone: 'ok',
        title: `Joined ${name}.`,
        body: wifi.ip ? `The clock's address there is ${wifi.ip}.` : undefined,
      };
    case 'failed_auth':
      return {
        tone: 'fail',
        title:
          security === 'enterprise'
            ? `${name} turned down the username or password.`
            : `${name} turned down the password.`,
        body: extra,
      };
    case 'failed_no_ap':
      return {
        tone: 'fail',
        title: `The clock can't find ${name}.`,
        body: 'Check the name, and that the clock is in range of the access point.',
      };
    case 'failed_timeout':
      return {
        tone: 'fail',
        title: `${name} didn't answer in time.`,
        body: extra ?? 'Try again, or move the clock closer to the access point.',
      };
    default:
      return null;
  }
}

/** Last scan result, polled fast while a scan runs. Starts a scan if the last one is old. */
function useScan() {
  const [starting, setStarting] = useState(false);
  const poll = usePoll(getWifiScan, starting ? 800 : 15_000);
  const data = poll.data;
  const scanning = starting || !!data?.scanning;
  const autoStarted = useRef(false);

  const start = async () => {
    setStarting(true);
    try {
      await startWifiScan();
    } catch {
      // 409: a scan is already running; just follow it.
    }
    poll.refresh();
  };

  useEffect(() => {
    if (data && !data.scanning) setStarting(false);
  }, [data]);

  useEffect(() => {
    if (!data || autoStarted.current) return;
    autoStarted.current = true;
    if (data.age_sec === null || data.age_sec > 120) void start();
  }, [data]);

  // Strongest first, one row per name (mesh systems repeat the same name).
  const networks: WifiNetwork[] = [];
  for (const n of [...(data?.networks ?? [])].sort((a, b) => b.rssi - a.rssi)) {
    if (n.ssid && !networks.some((m) => m.ssid === n.ssid)) networks.push(n);
  }

  return { scanning, networks, start };
}
