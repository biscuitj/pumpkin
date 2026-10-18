import { signal } from '@preact/signals';

export const TABS = [
  { id: 'display', label: 'Display' },
  { id: 'pet', label: 'Pet' },
  { id: 'schedule', label: 'Schedule' },
  { id: 'connections', label: 'Connections' },
  { id: 'device', label: 'Device' },
] as const;
export type TabId = (typeof TABS)[number]['id'];

function fromHash(): TabId {
  const id = location.hash.replace(/^#\/?/, '');
  return TABS.find((t) => t.id === id)?.id ?? TABS[0].id;
}

export const currentTab = signal<TabId>(fromHash());
window.addEventListener('hashchange', () => {
  currentTab.value = fromHash();
});
