"""Run in a temporary directory; pass shaders.rpf to exercise the selected original too."""
import hashlib
import os
from pathlib import Path
import shlex
import struct
import subprocess
import sys
import tempfile

from local_record import read_native
from native_ps import lower, need


def encode(*words):
    return struct.pack(f"<{len(words)}I", *words)


# Independent, project-owned instruction fixture. Four distinct components, nonsequential
# registers, overwritten value, reversed pack/export order, and no metadata/padding.
def mov(dst, src):
    return 0x7e000200 | dst << 17 | src


def pair(dst, lo, hi):
    return 0x5e000100 | dst << 17 | hi << 9 | lo


synthetic = encode(mov(19, 128), mov(23, 245), mov(7, 240), mov(9, 243), mov(19, 246),
                   pair(11, 7, 23), pair(18, 9, 19), 0xf8001c0f, 11 << 8 | 18, 0xbf810000)
bad = [synthetic[:-1], synthetic[:-4], synthetic[4*2:], encode(0xbf810000),
       synthetic + encode(0), encode(0xd5800000, 0, 0) + synthetic,
       encode(mov(12, 42)) + synthetic, synthetic[:-12] + encode(0xf8001c0f, 1, 0xbf810000)]
for invalid in bad:
    try:
        lower(invalid)
    except ValueError:
        continue
    raise AssertionError("invalid input accepted")

cases = [("project-owned", synthetic, (-1., 4., .5, -2.))]
if len(sys.argv) == 2:
    name = "PS_clearRipple_57383fac_Wrapped"
    original = read_native(sys.argv[1], name)
    # Identity assertion belongs to the fixture only. The translator takes only ISA bytes.
    need(hashlib.sha256(original).hexdigest() ==
         "55842ecafc14e16a56ff14e7511613939d16b61b0e8b0d87fa44414e5f115dee", "wrong test input")
    cases.append((name, original, (.5, .5, 1., 0.)))
else:
    need(len(sys.argv) == 1, "usage: python3 test.py [shaders.rpf]")

source = Path(__file__).resolve().with_name("draw.c")


def flags(key):
    return shlex.split(os.environ.get(key, ""))


with tempfile.TemporaryDirectory() as work:
    def run(*args):
        subprocess.run(args, cwd=work, check=True)

    run(os.environ.get("CC", "cc"), "-std=c11", "-Wall", "-Wextra", "-Werror", *flags("CPPFLAGS"),
        source, *flags("LDFLAGS"), "-lvulkan", "-o", "draw")

    def compile_module(filename, text):
        Path(work, filename).write_text(text)
        run("glslangValidator", "-V", filename, "-o", filename+".spv")
        run("spirv-val", "--target-env", "vulkan1.0", filename+".spv")

    compile_module("tri.vert", """#version 450
void main() {
    vec2 p[3]=vec2[3](vec2(-1,-1),vec2(3,-1),vec2(-1,3));
    gl_Position=vec4(p[gl_VertexIndex],0,1);
}""")
    for label, code, rgba in cases:
        compile_module("native.frag", lower(code))
        run("./draw", "tri.vert.spv", "native.frag.spv", "pixels")
        data = Path(work, "pixels").read_bytes()
        need(len(data) == 384, "pixel extent")
        for i, pixel in enumerate(struct.iter_unpack("<4e", data)):
            want = rgba if 1 <= i%8 <= 6 and 1 <= i//8 <= 4 else (1.,0.,1.,1.)
            need(pixel == want, f"{label} pixel {i}: {pixel} != {want}")
        print(f"PASS {label}: {len(code)} native bytes; fenced 24 x {rgba}, 24 magenta guards",
              flush=True)
