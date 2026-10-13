"""Host test for the firmware upload check (ESP32 header + Pumpkin marker)."""
import os
import shutil
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class OtaImageCheckTests(unittest.TestCase):
    def test_upload_check(self):
        with tempfile.TemporaryDirectory() as tmp:
            exe = Path(tmp) / 'ota-image-check'
            subprocess.run([
                os.environ.get('CXX', 'c++'), '-std=c++11', '-Wall', '-Wextra', '-Werror',
                '-I', str(ROOT / 'include'),
                str(ROOT / 'tests/test_ota_image_check.cpp'), '-o', str(exe),
            ], check=True)
            subprocess.run([str(exe)], check=True)


if __name__ == '__main__':
    unittest.main()

    def test_build_checks_the_same_marker(self):
        import importlib.util
        spec = importlib.util.spec_from_file_location('post_build', ROOT / 'scripts/post_build.py')
        post_build = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(post_build)
        self.assertEqual(post_build.image_marker(), b'PUMPKIN-IMAGE|ulanzi-tc001-esp32-4mb|')


@unittest.skipUnless(shutil.which('node'), 'Node required for the update page check')
class UpdatePageCheckTests(unittest.TestCase):
    """The update page refuses the same files as the clock, before uploading."""

    def test_page_check_matches_firmware_verdicts(self):
        html = (ROOT / 'data/www/device.html').read_text()
        start = html.index('function indexOfBytes')
        end = html.index('async function loadOta')
        functions = html[start:end]
        marker = 'PUMPKIN-IMAGE|ulanzi-tc001-esp32-4mb|'
        script = functions + r'''
let otaInfo = {image_id: %r};
function img(body, magic = 0xE9, chip = 0) {
  const head = new Uint8Array(24); head[0] = magic; head[12] = chip & 255; head[13] = chip >> 8;
  const filler = new Uint8Array(5000).map((_, i) => (i * 37) & 255);
  const b = new TextEncoder().encode(body);
  const out = new Uint8Array(head.length + filler.length + b.length + 100);
  out.set(head); out.set(filler, 24); out.set(b, 24 + filler.length);
  return {arrayBuffer: async () => out.buffer};
}
(async () => {
  const cases = [
    [img('\0' + otaInfo.image_id + '\0'), null],
    [img('esphome'), 'not_a_pumpkin_firmware'],
    [img('PUMPKIN-IMAGE|ulanzi-tc001-esp32-8mb|'), 'pumpkin_firmware_for_other_hardware'],
    [img(otaInfo.image_id, 0x50), 'not_esp32_firmware'],
    [img(otaInfo.image_id, 0xE9, 9), 'firmware_for_another_chip'],
    [{arrayBuffer: async () => new Uint8Array([0xE9, 1]).buffer}, 'firmware_file_too_short'],
  ];
  for (const [file, want] of cases) {
    const got = await checkFirmwareFile(file);
    if (got !== want) { console.error('got', got, 'want', want); process.exit(1); }
  }
  otaInfo = {};   // older status without image_id: leave it to the clock
  if (await checkFirmwareFile(img('esphome')) !== null) process.exit(1);
})();
''' % marker
        subprocess.run(['node', '-e', script], check=True)

    def test_page_shows_which_clock_before_installing(self):
        html = (ROOT / 'data/www/device.html').read_text()
        self.assertIn('id="ota-clock"', html)
        self.assertIn("on '+clockLabel(otaInfo)", html)
