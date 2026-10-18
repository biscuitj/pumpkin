import { useState } from 'preact/hooks';
import { getWifi } from '../api';
import type { WifiStatus } from '../contract';
import { usePoll } from '../lib/poll';
import { WifiJoin } from '../tabs/connections/WifiJoin';
import { Notice } from '../ui/Notice';
import { Logo } from '../ui/Logo';

/**
 * One job: get the clock onto Wi-Fi. Everything else happens in the dashboard,
 * on the clock's own address, once it's on the network.
 */
export function Setup() {
  const wifi = usePoll(getWifi, 1500);
  const w = wifi.data;
  const joined = w && w.trial.state === 'connected' && w.trial.ssid ? w : null;
  const [another, setAnother] = useState(false);

  return (
    <div class="shell setup">
      <header class="top">
        <Logo />
      </header>

      <main class="main setup__main">
        <div class="section__head">
          <h1 class="setup__title">Connect your clock to Wi-Fi</h1>
          <p class="section__lede">
            Pick the network the clock should use. It tries the network first and only keeps it once joining works.
          </p>
        </div>

        {joined ? <Joined w={joined} /> : null}

        {joined && !another ? (
          <div class="actions">
            <button type="button" class="btn" onClick={() => setAnother(true)}>
              Join a different network
            </button>
          </div>
        ) : w ? (
          <WifiJoin wifi={w} onStarted={wifi.refresh} />
        ) : wifi.error ? (
          <Notice tone="fail" title="Can't reach the clock.">
            Make sure this phone is still connected to the clock's hotspot, the network starting with Pumpkin-Setup.
          </Notice>
        ) : (
          <p class="muted">Asking the clock…</p>
        )}
      </main>

      <footer class="foot muted">
        After this, set up the pump connection and display from the clock's own page.
      </footer>
    </div>
  );
}

function Joined({ w }: { w: WifiStatus }) {
  const ssid = w.trial.ssid!;
  const address = w.local_name ?? w.ip;
  return (
    <Notice tone="ok" title={`The clock joined ${ssid}.`} live>
      <p>
        It closes this hotspot in a moment. Connect this phone to {ssid} again, then open{' '}
        {address ? <a href={`http://${address}`}>{address}</a> : "the clock's address"} to finish setting up: the pump
        connection, screens and schedule.
      </p>
    </Notice>
  );
}
