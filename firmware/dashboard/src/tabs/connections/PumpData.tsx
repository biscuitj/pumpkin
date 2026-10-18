import { useEffect, useRef, useState } from 'preact/hooks';
import { getSourceTest, saveSource, startSourceTest, toApiError } from '../../api';
import type { SourceKind, SourceStatus, SourceTest } from '../../contract';
import { glucose } from '../../lib/format';
import { status } from '../../state';
import { Field } from '../../ui/Field';
import { PasswordInput, Segmented, TextInput } from '../../ui/inputs';
import { Notice } from '../../ui/Notice';
import { describeReport } from './describe';

const KINDS = [
  { value: 'bridge', label: 'Pumpkin bridge' },
  { value: 'demo', label: 'Demo data' },
] as const;

type Errors = { bridge_url?: string; token?: string; form?: string };

interface Props {
  source: SourceStatus | null;
  onSaved: () => void;
}

export function PumpData({ source, onSaved }: Props) {
  const cfg = source?.config;
  const mmol = status.value?.use_mmol ?? false;

  const [kind, setKind] = useState<SourceKind>('bridge');
  const [url, setUrl] = useState('');
  const [token, setToken] = useState('');
  const [replacingToken, setReplacingToken] = useState(false);
  const [errors, setErrors] = useState<Errors>({});
  const [saving, setSaving] = useState(false);
  const [saved, setSaved] = useState(false);
  const test = useSourceTest();
  const loaded = useRef(false);

  // Fill the form once from the clock; after that the form is the user's.
  useEffect(() => {
    if (cfg && !loaded.current) {
      loaded.current = true;
      setKind(cfg.kind);
      setUrl(cfg.bridge_url);
    }
  }, [cfg]);

  if (!source || !cfg) {
    return (
      <section class="section" aria-labelledby="pump-h">
        <h2 id="pump-h">Pump data</h2>
        <p class="muted">Asking the clock…</p>
      </section>
    );
  }

  const askToken = kind === 'bridge' && (!cfg.token_set || replacingToken);
  const dirty = kind !== cfg.kind || (kind === 'bridge' && (url.trim() !== cfg.bridge_url || token !== ''));
  const current = describeReport(source, cfg.bridge_url, glucose(source.glucose_mgdl, mmol));

  const validate = (): Errors => {
    const e: Errors = {};
    const u = url.trim();
    if (!u) e.bridge_url = 'Enter the bridge address.';
    else if (!/^https:\/\/[^/\s]+/.test(u)) e.bridge_url = 'Use the https:// address of the bridge.';
    if (askToken && !token) e.token = 'Enter the clock token.';
    return e;
  };

  const edit = <T,>(set: (v: T) => void) => (v: T) => {
    set(v);
    setSaved(false);
    setErrors({});
  };

  const onTest = () => {
    const e = validate();
    setErrors(e);
    if (e.bridge_url || e.token) return;
    void test.run({ bridge_url: url.trim(), token: askToken ? token : undefined }).catch((err) => {
      const a = toApiError(err);
      setErrors(a.field ? { [a.field]: a.message } : { form: a.message });
    });
  };

  const onSave = async (ev: Event) => {
    ev.preventDefault();
    const e = kind === 'bridge' ? validate() : {};
    setErrors(e);
    if (e.bridge_url || e.token) return;
    setSaving(true);
    try {
      await saveSource(
        kind === 'bridge' ? { kind, bridge_url: url.trim(), token: token || undefined } : { kind },
      );
      setToken('');
      setReplacingToken(false);
      setSaved(true);
      test.reset();
      onSaved();
    } catch (err) {
      const a = toApiError(err);
      setErrors(a.field ? { [a.field]: a.message } : { form: a.message });
    } finally {
      setSaving(false);
    }
  };

  const testMsg = test.result
    ? describeReport(test.result, url.trim(), glucose(test.result.glucose_mgdl, mmol))
    : null;

  return (
    <section class="section" aria-labelledby="pump-h">
      <div class="section__head">
        <h2 id="pump-h">Pump data</h2>
        <p class="section__lede">
          The clock gets readings from your Pumpkin bridge, which signs in to CareLink as a care partner.
        </p>
      </div>

      <Notice tone={current.tone} title={current.title} live>
        {current.body}
      </Notice>

      <form class="form" onSubmit={onSave} noValidate>
        <Segmented name="source-kind" label="Readings from" value={kind} options={KINDS} onChange={edit(setKind)} />

        {kind === 'demo' ? (
          <p class="muted">
            Made-up readings that wander through lows, highs and pump alerts, so every screen can be tried without a
            pump.
          </p>
        ) : (
          <>
            <Field
              id="bridge-url"
              label="Bridge address"
              error={errors.bridge_url}
              hint={
                <>
                  The bridge's <code>/clock</code> address, like https://pumpkin-bridge.yourname.workers.dev/clock
                </>
              }
            >
              <TextInput
                id="bridge-url"
                type="url"
                value={url}
                onInput={edit(setUrl)}
                maxLength={255}
                placeholder="https://…/clock"
                invalid={!!errors.bridge_url}
              />
            </Field>

            {askToken ? (
              <Field
                id="bridge-token"
                label="Clock token"
                error={errors.token}
                hint={
                  <>
                    The <code>CLOCK_TOKEN</code> secret you set on the bridge.
                  </>
                }
              >
                <PasswordInput id="bridge-token" value={token} onInput={edit(setToken)} maxLength={255} invalid={!!errors.token} />
              </Field>
            ) : (
              <div class="field">
                <span class="field__label">Clock token</span>
                <div class="saved-secret">
                  <span>Saved on the clock</span>
                  <button type="button" class="btn btn--quiet" onClick={() => setReplacingToken(true)}>
                    Replace
                  </button>
                </div>
              </div>
            )}
          </>
        )}

        {testMsg ? (
          <Notice tone={testMsg.tone} title={`Test result: ${testMsg.title}`} live>
            {testMsg.body}
          </Notice>
        ) : null}
        {errors.form ? <Notice tone="fail" title={errors.form} live /> : null}

        <div class="actions">
          <button type="submit" class="btn btn--primary" disabled={!dirty || saving}>
            {saving ? 'Saving…' : 'Save'}
          </button>
          {kind === 'bridge' ? (
            <button type="button" class="btn" onClick={onTest} disabled={test.running}>
              {test.running ? 'Testing…' : 'Test connection'}
            </button>
          ) : null}
          {saved && !dirty ? (
            <span class="actions__note" role="status">
              Saved.
            </span>
          ) : null}
        </div>
      </form>
    </section>
  );
}

/** Start a test on the clock and poll until it reports a result. */
function useSourceTest() {
  const [running, setRunning] = useState(false);
  const [result, setResult] = useState<SourceTest['result']>(null);
  const alive = useRef(true);
  useEffect(() => () => void (alive.current = false), []);

  const run = async (req: Parameters<typeof startSourceTest>[0]) => {
    setRunning(true);
    setResult(null);
    try {
      await startSourceTest(req);
      for (let i = 0; i < 40 && alive.current; i++) {
        await new Promise((r) => setTimeout(r, 700));
        const t = await getSourceTest();
        if (t.state === 'done') {
          if (alive.current) setResult(t.result);
          return;
        }
      }
      throw new Error('The test took too long. Try again.');
    } finally {
      if (alive.current) setRunning(false);
    }
  };

  return { running, result, run, reset: () => setResult(null) };
}
