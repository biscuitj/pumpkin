import { currentTab, TABS } from './router';
import { connection, lastError, status } from './state';
import { ConnectionsTab } from './tabs/connections';
import { DeviceTab } from './tabs/device';
import { DisplayTab } from './tabs/display';
import { PetTab } from './tabs/pet';
import { ScheduleTab } from './tabs/schedule';
import { Logo } from './ui/Logo';

const LABEL = { connecting: 'Connecting', online: 'Connected', offline: 'Clock offline' } as const;
const TONE = { connecting: 'idle', online: 'ok', offline: 'fail' } as const;

export function App() {
  const conn = connection.value;
  const s = status.value;
  const tab = currentTab.value;

  return (
    <div class="shell">
      <header class="top">
        <Logo />
        <span class="pill" title={lastError.value ?? undefined}>
          <span class={`led led--${TONE[conn]}`} aria-hidden="true" />
          {LABEL[conn]}
        </span>
      </header>

      <nav class="tabs" aria-label="Sections">
        {TABS.map((t) => (
          <a key={t.id} href={`#/${t.id}`} class="tabs__link" aria-current={t.id === tab ? 'page' : undefined}>
            {t.label}
          </a>
        ))}
      </nav>

      <main class="main">
        {tab === 'display' ? <DisplayTab /> : null}
        {tab === 'pet' ? <PetTab /> : null}
        {tab === 'schedule' ? <ScheduleTab /> : null}
        {tab === 'connections' ? <ConnectionsTab /> : null}
        {tab === 'device' ? <DeviceTab /> : null}
      </main>

      <footer class="foot muted">{s ? `Firmware ${s.firmware_version}` : null}</footer>
    </div>
  );
}
