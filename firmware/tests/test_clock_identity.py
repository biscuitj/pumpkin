"""Host test for clock naming (default names, setup SSID, .local labels, copy groups)."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class ClockIdentityTests(unittest.TestCase):
    def test_names_slugs_and_copy_groups(self):
        with tempfile.TemporaryDirectory() as tmp:
            exe = Path(tmp) / 'clock-identity'
            subprocess.run([
                os.environ.get('CXX', 'c++'), '-std=c++11', '-Wall', '-Wextra', '-Werror',
                '-I', str(ROOT / 'include'),
                str(ROOT / 'tests/test_clock_identity.cpp'), '-o', str(exe),
            ], check=True)
            subprocess.run([str(exe)], check=True)


if __name__ == '__main__':
    unittest.main()
