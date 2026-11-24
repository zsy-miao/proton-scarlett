"""Validate a synthetic ordered display launch: ordered-display.py RUN radv|nvidia."""
from pathlib import Path
import re
import subprocess
import sys

run = Path(sys.argv[1])
log = (run / "logs/steam-999001.log").read_text(errors="replace")

def need(pattern):
    match = re.search(pattern, log, re.M | re.I)
    if not match:
        sys.exit(f"Missing evidence: {pattern}")
    return match

route = {"radv": "1002:164e path=wb-section", "nvidia": "10de:2484 path=cpu-control"}[sys.argv[2]]
need(rf"^TAIL adapter={route}$")
generation = need(r"PASS singleton token=1 count=1 generation=(\d+)").group(1)
end = need(rf"PASS ordered token=2 count=2 producer_end=([0-9a-f]+) generation={generation} wrong-key/end rejected").group(1)
need(rf"WSI lease: primary=\w+ token=2 serial=\d+ fence={end} entry=1")
# Use the clock's actual producer/batch identities. Its earlier submit printf can
# interleave with Wine thread-name diagnostics in the shared stderr.
clock = re.findall(rf"DETAIL clock tok=2 latest=2 producer=(\w+)/\1 batch=(\w+)/{end}\b", log, re.I)
if len(clock) != 2 or clock[0][0] != clock[1][0] or int(clock[0][1],16) == int(end,16) or int(clock[1][1],16) != int(end,16):
    sys.exit("Expected the same queue, a rejected foreign end, then the original batch end")
if re.findall(r"^WSI count hr=0 before=(\d+) after=(\d+)", log, re.M) != [("0","1"),("1","2")]:
    sys.exit("Expected WSI count 0→1→2")
if re.findall(r"private PresentX result=0 token=(\d+)", log) != ["1","2"]:
    sys.exit("Expected successful captured-window PresentX(1), PresentX(2)")
subprocess.run([sys.executable, str(Path(__file__).with_name("offscreen-pixels.py")), str(run / "display.mp4")], check=True)
print(f"PASS ordered display: {route}, generation {generation}, producer end {end}")
