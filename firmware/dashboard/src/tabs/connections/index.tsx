import { useEffect, useState } from 'preact/hooks';
import { getSource, getWifi } from '../../api';
import type { WifiStatus } from '../../contract';
import { usePoll } from '../../lib/poll';
import { Notice } from '../../ui/Notice';
import { ConnectionPath } from './ConnectionPath';
import { connectionHops } from './describe';
import { PumpData } from './PumpData';
import { Wifi } from './Wifi';

export function ConnectionsTab() {
  // Poll faster while something is changing, so progress feels live.
  const [busy, setBusy] = useState(false);
  const wifi = usePoll(getWifi, busy ? 900 : 4000);
  const source = usePoll(getSource, 5000);
  const nowBusy = isBusy(wifi.data);
  useEffect(() => setBusy(nowBusy), [nowBusy]);

  const error = wifi.error ?? source.error;

  return (
    <div class="tab">
      <ConnectionPath hops={connectionHops(wifi.data, source.data)} />
      {error && !wifi.data ? <Notice tone="fail" title={error.message} /> : null}
      <Wifi
        wifi={wifi.data}
        onChanged={() => {
          wifi.refresh();
          source.refresh();
        }}
      />
      <PumpData source={source.data} onSaved={source.refresh} />
    </div>
  );
}

function isBusy(w: WifiStatus | null): boolean {
  if (!w) return false;
  const t = w.trial.state;
  return w.mode === 'connecting' || t === 'associating' || t === 'authenticating' || w.checks.running;
}
