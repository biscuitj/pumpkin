import { useState } from 'preact/hooks';
import { recheckNetwork } from '../../api';
import type { NetChecks, WifiStatus } from '../../contract';
import { SignalBars } from '../../ui/icons';
import { Notice } from '../../ui/Notice';
import { CHECK_LABEL } from './describe';
import { WifiJoin } from './WifiJoin';

interface Props {
  wifi: WifiStatus | null;
  onChanged: () => void;
}

export function Wifi({ wifi, onChanged }: Props) {
  const [joinOpen, setJoinOpen] = useState(false);

  if (!wifi) {
    return (
      <section class="section" aria-labelledby="wifi-h">
        <h2 id="wifi-h">Wi-Fi</h2>
        <p class="muted">Asking the clock…</p>
      </section>
    );
  }

  const connected = wifi.mode === 'connected';
  const showJoin = joinOpen || !connected;

  return (
    <section class="section" aria-labelledby="wifi-h">
      <div class="section__head">
        <h2 id="wifi-h">Wi-Fi</h2>
      </div>

      {connected ? (
        <div class="network">
          <SignalBars rssi={wifi.rssi} />
          <div class="network__text">
            <p class="network__name">{wifi.ssid}</p>
            <p class="muted">
              {wifi.security === 'enterprise' ? 'School or work login. ' : ''}
              {wifi.ip ? `Address ${wifi.ip}` : ''}
            </p>
          </div>
          {wifi.local_name ? (
            <a class="network__link" href={`http://${wifi.local_name}`}>
              {wifi.local_name}
            </a>
          ) : null}
        </div>
      ) : wifi.mode === 'setup_ap' && wifi.ap ? (
        <Notice tone="warn" title={`The clock is running its setup hotspot, ${wifi.ap.ssid}.`}>
          Choose the Wi-Fi network it should use below. Until then, it can only be reached at {wifi.ap.ip} from a device
          on that hotspot.
        </Notice>
      ) : wifi.mode === 'connecting' ? (
        <Notice tone="idle" title={`Connecting to ${wifi.ssid ?? 'Wi-Fi'}…`} live />
      ) : (
        <Notice tone="fail" title="The clock is not connected to Wi-Fi." />
      )}

      {connected ? <Checks checks={wifi.checks} onRecheck={onChanged} /> : null}

      {showJoin ? (
        <WifiJoin wifi={wifi} onStarted={onChanged} onCancel={connected ? () => setJoinOpen(false) : undefined} />
      ) : (
        <div class="actions">
          <button type="button" class="btn" onClick={() => setJoinOpen(true)}>
            Join another network
          </button>
        </div>
      )}
    </section>
  );
}

const CHECKS: { key: 'dns' | 'data' | 'ntp'; label: string; fix: (host: string) => string }[] = [
  {
    key: 'dns',
    label: 'Name lookups',
    fix: () => "The network doesn't answer DNS lookups for the clock. Ask IT to allow DNS.",
  },
  {
    key: 'data',
    label: 'Bridge over HTTPS',
    fix: (host) => `HTTPS to ${host} is blocked. Ask IT to allow it.`,
  },
  {
    key: 'ntp',
    label: 'Time sync',
    fix: () =>
      "UDP port 123 (NTP) is blocked, so the clock can't set its time and may call fresh readings old. Ask IT to allow NTP.",
  },
];

function Checks({ checks, onRecheck }: { checks: NetChecks; onRecheck: () => void }) {
  const [busy, setBusy] = useState(false);
  const recheck = async () => {
    setBusy(true);
    try {
      await recheckNetwork();
      onRecheck();
    } finally {
      setBusy(false);
    }
  };

  return (
    <div class="checks">
      <p class="checks__intro muted">
        A school network can let the clock on and still block what it needs. These are checked after every connection.
      </p>
      <dl class="checks__list">
        {CHECKS.map((c) => {
          const r = checks[c.key];
          return (
            <div key={c.key} class={`check check--${r}`}>
              <dt>{c.label}</dt>
              <dd>
                <span class={`led led--${r === 'ok' ? 'ok' : r === 'fail' ? 'fail' : 'idle'}`} aria-hidden="true" />
                {CHECK_LABEL[r]}
              </dd>
              {r === 'fail' ? <p class="check__fix">{c.fix(checks.data_host ?? 'the bridge')}</p> : null}
            </div>
          );
        })}
      </dl>
      <button type="button" class="btn btn--quiet" onClick={recheck} disabled={busy || checks.running}>
        {checks.running ? 'Checking…' : 'Check again'}
      </button>
    </div>
  );
}
