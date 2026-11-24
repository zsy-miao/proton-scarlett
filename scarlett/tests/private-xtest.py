"""Hold one keysym for a bounded time on the harness's isolated Xvfb only."""
import ctypes as C
from pathlib import Path
import socket
import struct
import sys
import time


def hold(keysym, milliseconds, window):
    if not 1 <= milliseconds <= 2000:
        raise ValueError("hold must be 1..2000 ms")
    if Path("/proc/1/comm").read_text().strip() != "bwrap" or not Path("/work/lock").is_file():
        sys.exit("refusing input outside the private harness")
    pid = int(Path("/tmp/.X93-lock").read_text())
    if Path(f"/proc/{pid}/exe").resolve(strict=True).name != "Xvfb":
        sys.exit("private Xvfb not found")
    x = C.CDLL("libX11.so.6")
    xt = C.CDLL("libXtst.so.6")
    x.XOpenDisplay.restype = C.c_void_p
    d = C.c_void_p(x.XOpenDisplay(b"unix/:93"))
    if not d:
        sys.exit("cannot open private Xvfb")
    try:
        with socket.fromfd(x.XConnectionNumber(d), socket.AF_UNIX, socket.SOCK_STREAM) as s:
            peer, _, _ = struct.unpack("3i", s.getsockopt(socket.SOL_SOCKET, socket.SO_PEERCRED, 12))
        if peer != pid:
            sys.exit("X connection is not to the harness's Xvfb")
        if not 1 < window <= 0xffffffff:
            sys.exit("expected a target window XID")
        x.XSetInputFocus(d, C.c_ulong(window), 1, C.c_ulong(0))
        focus, revert = C.c_ulong(), C.c_int()
        x.XGetInputFocus(d, C.byref(focus), C.byref(revert))
        if focus.value != window:
            sys.exit("target did not receive X focus")
        x.XStringToKeysym.restype = C.c_ulong
        x.XKeysymToKeycode.restype = C.c_ubyte
        key = x.XKeysymToKeycode(d, C.c_ulong(x.XStringToKeysym(keysym.encode("ascii"))))
        if not key or not xt.XTestFakeKeyEvent(d, key, 1, C.c_ulong(0)):
            sys.exit("XTEST press failed")
        try:
            x.XSync(d, 0)
            time.sleep(milliseconds / 1000)
        finally:
            if not xt.XTestFakeKeyEvent(d, key, 0, C.c_ulong(0)):
                sys.exit("XTEST release failed")
            x.XSync(d, 0)
        print(f"XTEST private Xvfb pid={pid} window={window:#x}: {keysym} held {milliseconds} ms", flush=True)
    finally:
        x.XCloseDisplay(d)


if __name__ == "__main__":
    if len(sys.argv) != 4:
        sys.exit("usage: private-xtest.py KEYSYM HOLD_MS WINDOW_XID")
    hold(sys.argv[1], int(sys.argv[2]), int(sys.argv[3], 0))
