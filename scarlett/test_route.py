"""Check the Steam command line without requiring a GPU or a title package."""
import os
from pathlib import Path
import runpy
import sys
from tempfile import TemporaryDirectory
from unittest.mock import patch

root = Path(__file__).resolve().parents[1]
sys.dont_write_bytecode = True
sys.path.insert(0, str(root))
ns = runpy.run_path(str(root / "proton"))
session = ns["Session"]()
session.remote_debug_cmd = None
session.cmdlineappend = ["extra value"]
session.env.pop("SteamGameId", None)
with TemporaryDirectory() as d, patch("shutil.which", return_value=None):
    host = Path(d, "scarlett", "xcl.exe")
    session.run.__globals__["g_proton"] = ns["Proton"](d)
    session.run_proc = lambda args: args
    with patch.object(sys, "argv", ["proton", "run", "regular.exe"]):
        assert session.run()[-3:] == ["c:\\windows\\system32\\steam.exe", "regular.exe", "extra value"]
    game = r"Z:\My Game\game_scarlett_master.exe"
    with patch.object(sys, "argv", ["proton", "run", game, "-arg"]):
        assert session.run() == 1
        host.parent.mkdir()
        host.touch()
        assert session.run() == [d + "/files/bin/wine", str(host), "run", game, "--", "-arg", "extra value"]
        assert session.env["XCL_NETWORK"] == "0"
    with patch.object(sys, "argv", ["proton", "run", "/My Game/game_scarlett_master.exe"]), \
         patch("subprocess.run") as winepath:
        winepath.return_value.returncode = 0
        winepath.return_value.stdout = os.fsencode(game) + b"\n"
        assert session.run() == [d + "/files/bin/wine", str(host), "run", game, "--", "extra value"]
        assert winepath.call_args.args[0][-1] == sys.argv[2]
