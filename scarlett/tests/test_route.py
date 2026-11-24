"""Check Steam dispatch and DLL path isolation without starting Wine."""
from pathlib import Path
import runpy
import sys
from tempfile import TemporaryDirectory
import unittest
from unittest.mock import patch

root = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(root))
proton = runpy.run_path(str(root / "proton"))


class LaunchTest(unittest.TestCase):
    def setUp(self):
        tmp = TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        self.base = Path(tmp.name)
        self.tool = self.base / "tool"
        self.provider = self.tool / "scarlett/GameInput.dll"
        self.provider.parent.mkdir(parents=True)
        self.provider.touch()
        patcher = patch.dict(proton["Session"].run.__globals__, g_proton=proton["Proton"](str(self.tool)))
        patcher.start()
        self.addCleanup(patcher.stop)

    def dispatch(self, target, verb="run"):
        s = proton["Session"]()
        s.env["WINEPATH"] = r"C:\old"
        s.env["WINEDLLOVERRIDES"] = "existing=b"
        s.remote_debug_cmd = None
        s.cmdlineappend = []
        s.run_proc = lambda args: args
        argv = ["proton", verb, target, "argument with spaces"]
        data = proton["CompatData"](str(self.base), proton["is_scarlett_launch"](argv))
        if data.scarlett:
            (Path(data.prefix_dir) / "dosdevices").mkdir(parents=True, exist_ok=True)
            dest = Path(data.prefix_dir) / "drive_c/windows/system32/gameinput.dll"
            dest.parent.mkdir(parents=True, exist_ok=True)
            data.pfx_copy("unused", str(dest))
        with patch.dict(s.run.__globals__, g_compatdata=data), \
             patch("shutil.which", return_value=None), \
             patch.object(sys, "argv", argv), \
             patch("subprocess.run") as winepath:
            winepath.return_value.returncode = 0
            winepath.return_value.stdout = b"Z:\\tool\\scarlett\n"
            return s.run(), s.env["WINEPATH"], s.env["WINEDLLOVERRIDES"], winepath.call_count

    def test_scarlett(self):
        target = "/My Game/game_scarlett_master.exe"
        orig = self.base / "pfx/drive_c/windows/system32/gameinput.dll"
        orig.parent.mkdir(parents=True)
        orig.write_bytes(b"existing")
        args, path, overrides, calls = self.dispatch(target, "waitforexitandrun")
        self.assertEqual(args, [str(self.tool / "files/bin/wine"), target, "argument with spaces"])
        self.assertEqual((path, overrides, calls), (r"Z:\tool\scarlett;C:\old", "existing=b;gameinput=n", 1))
        self.assertEqual(orig.read_bytes(), b"existing")
        self.assertEqual((self.base / "scarlett-title/pfx/dosdevices/g:").readlink(), Path("/My Game"))

    def test_conflict(self):
        conflicting = self.base / "scarlett-title/pfx/drive_c/windows/system32/gameinput.dll"
        conflicting.parent.mkdir(parents=True)
        conflicting.write_bytes(b"keep me")
        with self.assertRaisesRegex(ValueError, "Refusing to replace"):
            self.dispatch("GAME_SCARLETT_MASTER.EXE")
        self.assertEqual(conflicting.read_bytes(), b"keep me")

    def test_runtime_system32(self):
        runtime = self.provider.with_name("xgameruntime.dll")
        runtime.touch()
        self.dispatch("game_scarlett_master.exe")
        slot = self.base / "scarlett-title/pfx/drive_c/windows/system32/xgameruntime.dll"
        self.assertTrue(slot.samefile(runtime))
        self.assertFalse((self.base / "pfx/drive_c/windows/system32/xgameruntime.dll").exists())

    def test_desktop(self):
        args, path, overrides, calls = self.dispatch("regular.exe")
        self.assertEqual(args[-2:], ["regular.exe", "argument with spaces"])
        self.assertTrue(args[-3].endswith("steam.exe"))
        self.assertEqual((path, overrides, calls), (r"C:\old", "existing=b", 0))
        self.assertFalse((self.base / "scarlett-title").exists())

    def test_scratch_drive(self):
        data = proton["CompatData"](str(self.base), True)
        drive = Path(data.prefix_dir) / "dosdevices/t:"
        drive.parent.mkdir(parents=True)
        with patch.dict(proton["setup_steam_dir_drive"].__globals__, g_compatdata=data):
            proton["setup_steam_dir_drive"]()
            (drive / "report").write_text("test")
            proton["setup_steam_dir_drive"]()
            self.assertEqual((self.base / "scarlett-title/scratch/report").read_text(), "test")
            drive.unlink()
            drive.symlink_to(self.base)
            with self.assertRaisesRegex(ValueError, "Conflicting"):
                proton["setup_steam_dir_drive"]()
            self.assertEqual(drive.readlink(), self.base)


if __name__ == "__main__":
    unittest.main()
