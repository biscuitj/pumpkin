import { IMAGE_MARKER, type UpdateError } from '../contract';

/** The OTA slot in partitions_custom.csv: 0x1C0000 bytes. */
export const MAX_IMAGE_BYTES = 0x1c0000;

export type ImageProblem = UpdateError | 'too_big';

export const UPDATE_MESSAGES: Record<ImageProblem, string> = {
  not_firmware: "That file isn't clock firmware. Pick the firmware.bin from a Pumpkin release.",
  wrong_chip: 'That firmware is for a different chip than the one in this clock.',
  not_pumpkin: "That's firmware for an ESP32, but not Pumpkin. The clock only installs Pumpkin firmware.",
  wrong_hardware: 'That Pumpkin firmware is built for different hardware than this clock.',
  truncated: 'The file is incomplete. Download it again.',
  too_big: "The file is bigger than the clock's update space, so it can't be Pumpkin firmware for this clock.",
  write_failed: "The clock couldn't write the update. It keeps its current firmware; try again.",
  busy: 'An update is already running.',
};

/**
 * The same checks the clock runs while the file streams in, done first in the
 * browser so a wrong file is caught before a 1.5 MB upload.
 */
export async function checkImage(file: Blob): Promise<ImageProblem | null> {
  if (file.size > MAX_IMAGE_BYTES) return 'too_big';
  const buf = new Uint8Array(await file.arrayBuffer());
  if (buf.length < 24) return 'truncated';
  if (buf[0] !== 0xe9) return 'not_firmware';
  if ((buf[12] | (buf[13] << 8)) !== 0) return 'wrong_chip';
  const text = new TextDecoder('latin1').decode(buf);
  if (text.includes(IMAGE_MARKER)) return null;
  return text.includes('PUMPKIN-IMAGE|') ? 'wrong_hardware' : 'not_pumpkin';
}

export const formatBytes = (n: number) =>
  n >= 1_000_000 ? `${(n / 1_000_000).toFixed(2)} MB` : `${Math.round(n / 1000)} KB`;
