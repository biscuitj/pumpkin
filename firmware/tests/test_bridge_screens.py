"""Run the production display engine against bridge extras (sport mode, IOB, pump alerts)."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class BridgeScreensTests(unittest.TestCase):
    def test_sport_bars_iob_and_alerts_rotation(self):
        with tempfile.TemporaryDirectory() as tmp:
            exe = Path(tmp) / 'bridge-screens'
            subprocess.run([
                os.environ.get('CXX', 'c++'), '-std=c++11', '-Wall', '-Wextra',
                '-Werror', '-Wno-error=unused-variable',
                '-I', str(ROOT / 'tests/engine_stubs'), '-I', str(ROOT / 'include'),
                str(ROOT / 'tests/test_bridge_screens.cpp'),
                '-o', str(exe),
            ], check=True)
            subprocess.run([str(exe)], check=True)


if __name__ == '__main__':
    unittest.main()
