"""Host test for button double-click detection and the LibreLinkUp session logic."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class HostLogicTests(unittest.TestCase):
    def test_host_logic(self):
        with tempfile.TemporaryDirectory() as tmp:
            binary = os.path.join(tmp, "host-logic")
            subprocess.run([
                os.environ.get("CXX", "c++"), "-std=c++11", "-pthread", "-I", str(ROOT / "include"),
                str(ROOT / "tests/test_host_logic.cpp"),
                str(ROOT / "src/libre_session.cpp"),
                str(ROOT / "src/libre_config.cpp"),
                str(ROOT / "src/libre_patient.cpp"), "-o", binary,
            ], check=True)
            subprocess.run([binary], check=True)


if __name__ == "__main__":
    unittest.main()
