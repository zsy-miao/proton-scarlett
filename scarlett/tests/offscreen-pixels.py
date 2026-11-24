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


seen = False
for left, right in zip(samples(200), samples(600)):
    if near(left, (255, 0, 0)) and near(right, (0, 0, 255)):
        seen = True
    elif seen and near(left, (0, 255, 0)) and near(right, (255, 0, 255)):
        print("PASS: independently decoded red/blue then green/magenta")
        break
else:
    sys.exit("FAIL: expected both spatial patterns in order")
