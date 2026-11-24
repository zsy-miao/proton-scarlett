"""Negative qualification oracle. Input: local retail EXE and optional live capture directory."""
import argparse
import hashlib
import struct
from pathlib import Path

from local_record import dxbc, fields, need
from native_pair import link, rows, translate
from native_ps import lower


def rejected(reason, fn, *args, **kw):
    try:
        fn(*args, **kw)
    except ValueError as e:
        need(str(e) == reason, f"{fn.__name__}: unexpected rejection: {e}")
        print(f"{fn.__name__}: rejected: {e}")
    else:
        raise AssertionError(f"{fn.__name__}: qualified unexpectedly; update the oracle")


def stream_oracle(s):
    expected = bytearray(0x2b0)

    def put(off, *words):
        struct.pack_into(f"<{len(words)}I", expected, off, *words)

    for off, tag in [(0,0),(0x10,1),(0x28,2),(0x40,3),(0x58,4),(0x70,5),
                     (0x88,8),(0x1d8,9),(0x1e0,10),(0x210,21),(0x250,17),
                     (0x260,12),(0x278,14),(0x280,15),(0x2a8,16)]:
        put(off, tag)
    put(0x20, 992)
    put(0x38, 964)
    for rt in range(8):
        put(0x94+40*rt, 0,0,2,1,1,2,1,1,4,15 if rt == 0 else 0)
    put(0x1dc, 0xffffffff)
    put(0x1e4, 3,1,1)  # solid, no cull, CCW; depth clip off
    put(0x214, 0,0,8,0,0xff,1,1,1,8,1,1,1,8,0)
    put(0x254, 1,0)
    put(0x27c, 3)
    put(0x284, 28)
    put(0x2a4, 1)
    s = bytearray(s)
    for off in (8,0x18,0x30):
        need(fields(s,off,"Q")[0] != 0, "missing root/stage")
        s[off:off+8] = bytes(8)
    need(s == expected, "unexpected fixed state or stage extent")
    print("stream: exact fixed state and extents; 1 RGBA8_UNORM, triangles, sample 1:0")


def check(exe, capture):
    # Offsets within raw .data of the supplied build. Digest the exact span, not its label.
    records = []
    with exe.open("rb") as f:
        for kind, off, n, digest in (
            (1,0x688cd3c,992,"8574a817bd2754b2603987e45c8451ddc2ec1564a04e7e547f68a5c634a46bd9"),
            (0,0x688e86f,964,"c9f99cce129603afe795042cced590b5f25eb664150abff191f1498539aab819"),
        ):
            stage = "vs" if kind else "ps"
            f.seek(off)
            b = f.read(n)
            need(hashlib.sha256(b).hexdigest() == digest, f"unexpected {stage} container")
            if capture:
                need((capture / f"{stage}.bin").read_bytes() == b, f"live {stage} differs")
            if kind:
                rejected("PSV resources", dxbc, b, kind)
            r = dxbc(b, kind, inspect_raw_vs=True)
            print(f"{stage}: {n}B; ISA {len(r.code)}B; inputs {rows(r.inputs)}; "
                  f"outputs {rows(r.outputs)}; native resource rows {r.resources}")
            records.append(r)
    if capture:
        stream_oracle((capture / "stream.bin").read_bytes())
    vs, ps = records
    need(fields(vs.psv, 60, "7I") == (24,4,3,0,0,11,0) and not ps.resources, "root bindings")
    need(fields(ps.psv, 0x80, "II8B") == (1,0,1,1,0x44,0,3,1,0,0), "constant float4")
    rejected("unmatched float2 semantic/index/register/mask", link, vs, ps, hlsl=True)
    for r, vert in [(vs, True), (ps, False)]:
        rejected("unsupported native instruction", translate, r.code, vert, hlsl=True)
    rejected("unsupported instruction at byte 28", lower, ps.code)
    print("PASS: raw vertex fetch + flat float4 clear pair is unqualified")


if __name__ == "__main__":
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("exe", type=Path)
    ap.add_argument("capture", type=Path, nargs="?")
    args = ap.parse_args()
    check(args.exe, args.capture)
