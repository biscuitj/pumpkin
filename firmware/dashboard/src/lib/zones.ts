/**
 * Time zones the dashboard offers, with their POSIX rules. The clock has no zone
 * database: the dashboard sends the rule along with the id. Add zones here as
 * families in other places start using Pumpkin.
 */
export interface Zone {
  id: string;
  label: string;
  posix: string;
}

const CET = 'CET-1CEST,M3.5.0,M10.5.0/3';
const EET = 'EET-2EEST,M3.5.0/3,M10.5.0/4';

export const ZONE_GROUPS: { name: string; zones: Zone[] }[] = [
  {
    name: 'Europe',
    zones: [
      { id: 'Europe/Brussels', label: 'Brussels', posix: CET },
      { id: 'Europe/Amsterdam', label: 'Amsterdam', posix: CET },
      { id: 'Europe/Luxembourg', label: 'Luxembourg', posix: CET },
      { id: 'Europe/Paris', label: 'Paris', posix: CET },
      { id: 'Europe/Berlin', label: 'Berlin', posix: CET },
      { id: 'Europe/Zurich', label: 'Zurich', posix: CET },
      { id: 'Europe/Vienna', label: 'Vienna', posix: CET },
      { id: 'Europe/Rome', label: 'Rome', posix: CET },
      { id: 'Europe/Madrid', label: 'Madrid', posix: CET },
      { id: 'Europe/Copenhagen', label: 'Copenhagen', posix: CET },
      { id: 'Europe/Oslo', label: 'Oslo', posix: CET },
      { id: 'Europe/Stockholm', label: 'Stockholm', posix: CET },
      { id: 'Europe/Warsaw', label: 'Warsaw', posix: CET },
      { id: 'Europe/Prague', label: 'Prague', posix: CET },
      { id: 'Europe/London', label: 'London', posix: 'GMT0BST,M3.5.0/1,M10.5.0' },
      { id: 'Europe/Dublin', label: 'Dublin', posix: 'IST-1GMT0,M10.5.0,M3.5.0/1' },
      { id: 'Europe/Lisbon', label: 'Lisbon', posix: 'WET0WEST,M3.5.0/1,M10.5.0' },
      { id: 'Europe/Helsinki', label: 'Helsinki', posix: EET },
      { id: 'Europe/Athens', label: 'Athens', posix: EET },
      { id: 'Europe/Bucharest', label: 'Bucharest', posix: EET },
    ],
  },
  {
    name: 'Americas',
    zones: [
      { id: 'America/New_York', label: 'New York (Eastern)', posix: 'EST5EDT,M3.2.0,M11.1.0' },
      { id: 'America/Toronto', label: 'Toronto', posix: 'EST5EDT,M3.2.0,M11.1.0' },
      { id: 'America/Chicago', label: 'Chicago (Central)', posix: 'CST6CDT,M3.2.0,M11.1.0' },
      { id: 'America/Denver', label: 'Denver (Mountain)', posix: 'MST7MDT,M3.2.0,M11.1.0' },
      { id: 'America/Phoenix', label: 'Phoenix', posix: 'MST7' },
      { id: 'America/Los_Angeles', label: 'Los Angeles (Pacific)', posix: 'PST8PDT,M3.2.0,M11.1.0' },
    ],
  },
  {
    name: 'Elsewhere',
    zones: [
      { id: 'Australia/Sydney', label: 'Sydney', posix: 'AEST-10AEDT,M10.1.0,M4.1.0/3' },
      { id: 'Asia/Tokyo', label: 'Tokyo', posix: 'JST-9' },
      { id: 'UTC', label: 'UTC', posix: 'UTC0' },
    ],
  },
];

export const ZONES: Zone[] = ZONE_GROUPS.flatMap((g) => g.zones);
export const findZone = (id: string) => ZONES.find((z) => z.id === id) ?? null;

/** The zone this browser is in, if the dashboard knows it. */
export function browserZone(): Zone | null {
  try {
    return findZone(Intl.DateTimeFormat().resolvedOptions().timeZone);
  } catch {
    return null;
  }
}
