"""Compile and draw the checked native pairs through the bootstrap device's host."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

import test_pair
import test_sample


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("pack", nargs="?", help="optional local shaders.rpf")
    p.add_argument("--tool", help="installed Scarlett Proton (launcher default if omitted)")
    p.add_argument("--dxc", default="dxc", help="native dxc or path to x64 dxc.exe with adjacent DLLs")
    args = p.parse_args()
    dxc = shutil.which(args.dxc) or args.dxc
    if not Path(dxc).is_file():
        p.error("dxc not found; use --dxc")
    dxc = Path(dxc).resolve()
    here = Path(__file__).resolve().parent
    scarlett = here.parents[1]
    native = scarlett / "native"
    with tempfile.TemporaryDirectory(prefix="native-d3d12-") as tmp:
        work = Path(tmp)
        game = work / "game"
        game.mkdir()
        compiler = [dxc]
        env = os.environ.copy()
        if dxc.suffix.lower() == ".exe":
            prefix = work / "dxc-prefix"
            prefix.mkdir()
            env.update(WINEPREFIX=str(prefix), WINEDEBUG="-all")
            compiler.insert(0, "wine")
        checks = []
        for group, mod in (("pair", test_pair), ("sample", test_sample)):
            for i, (label, stages, oracle) in enumerate(mod.cases(args.pack, hlsl=True)):
                key = f"{group}{i}"
                for stage, source in zip(("vs", "ps"), stages):
                    base = f"{key}.{stage}"
                    (game / (base+".hlsl")).write_text(source)
                    subprocess.run([*compiler, "-Ges", "-Gis", "-T", stage+"_6_0", "-E", "main",
                                    "-Fo", base+".dxil", base+".hlsl"], cwd=game, env=env, check=True)
                checks.append((key, label, mod.check, oracle))
        for src in (here / "d3d12-device.c", native / "queue/queue.c"):
            subprocess.run(["x86_64-w64-mingw32-gcc", "-std=c11", "-O2", "-DSCARLETT_DEVICE_BOOTSTRAP",
                            "-I"+str(native), "-c", src, "-o", work/(src.stem+".o")], check=True)
        subprocess.run(["x86_64-w64-mingw32-g++", "-std=c++17", "-O2", "-Wall", "-Wextra", "-Werror",
                        "-static", here/"d3d12-draw.cpp", work/"d3d12-device.o", work/"queue.o",
                        "-ld3d12", "-o", game/"game_scarlett_master.exe"], check=True)
        launcher = [scarlett/"test-launch-nixos.sh", game]
        if args.tool:
            launcher.append(args.tool)
        run = subprocess.run(launcher, stdout=subprocess.PIPE, text=True)
        print(run.stdout, end="", flush=True)
        run.check_returncode()
        path, = (s.removeprefix("Test output (including prefix): ") for s in run.stdout.splitlines()
                 if s.startswith("Test output (including prefix): "))
        for key, label, check, oracle in checks:
            pixels, = Path(path).rglob(f"native-draw-{key}.pixels")
            check(pixels.read_bytes(), oracle)
            print("PASS D3D12", label, flush=True)


if __name__ == "__main__":
    main()
