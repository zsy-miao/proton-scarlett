"""Check Steam dispatch and DLL path isolation without starting Wine."""
from pathlib import Path
import runpy
import sys
import unittest
from unittest.mock import patch

root = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(root))
proton = runpy.run_path(str(root / "proton"))


class LaunchTest(unittest.TestCase):
    def dispatch(self, target):
        s = proton["Session"]()
        s.env["WINEPATH"] = r"C:\old"
        s.remote_debug_cmd = None
        s.cmdlineappend = []
        s.run_proc = lambda args: args
        with patch.dict(s.run.__globals__, g_proton=proton["Proton"]("/tool")), \
             patch("shutil.which", return_value=None), \
             patch.object(sys, "argv", ["proton", "run", target, "argument with spaces"]), \
             patch("subprocess.run") as winepath:
            winepath.return_value.returncode = 0
            winepath.return_value.stdout = b"Z:\\tool\\scarlett\n"
            return s.run(), s.env["WINEPATH"], winepath.call_count

    def test_scarlett(self):
        target = "/My Game/game_scarlett_master.exe"
        args, path, calls = self.dispatch(target)
        self.assertEqual(args, ["/tool/files/bin/wine", target, "argument with spaces"])
        self.assertEqual((path, calls), (r"Z:\tool\scarlett;C:\old", 1))

    def test_desktop(self):
        args, path, calls = self.dispatch("regular.exe")
        self.assertEqual(args[-2:], ["regular.exe", "argument with spaces"])
        self.assertTrue(args[-3].endswith("steam.exe"))
        self.assertEqual((path, calls), (r"C:\old", 0))


if __name__ == "__main__":
    unittest.main()
