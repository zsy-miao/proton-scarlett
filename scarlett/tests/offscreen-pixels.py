"""Verify the two rendered halves in the 800x600 private Sway recording."""
import subprocess
import sys


def samples(x):
    rgb = subprocess.check_output([
        "ffmpeg", "-nostdin", "-v", "error", "-i", sys.argv[1],
        "-vf", f"crop=2:2:{x}:300,scale=1:1,format=rgb24", "-f", "rawvideo", "-",
    ])
    return zip(*[iter(rgb)] * 3)


def near(rgb, want):
    return all(abs(a-b) < 10 for a, b in zip(rgb, want))


patterns = [((255,0,0),(0,0,255)), ((0,255,0),(255,0,255))]
if sys.argv[2:] == ["--third"]:
    patterns.append(((255,255,0),(0,255,255)))
elif sys.argv[2:]:
    sys.exit("usage: offscreen-pixels.py VIDEO [--third]")
stage = 0
for left, right in zip(samples(200), samples(600)):
    if near(left,patterns[stage][0]) and near(right,patterns[stage][1]):
        stage += 1
        if stage == len(patterns):
            print("PASS: independently decoded red/blue then green/magenta" + (" then yellow/cyan" if stage==3 else ""))
            break
else:
    sys.exit(f"FAIL: missing spatial pattern {stage+1} in order")
