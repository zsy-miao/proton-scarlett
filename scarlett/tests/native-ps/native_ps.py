"""Offline constant-only native pixel shader -> Vulkan GLSL. No guest execution."""
import struct


def need(ok, reason):
    if not ok:
        raise ValueError(reason)


def lower(code):
    need(0 < len(code) <= 4096 and len(code) % 4 == 0, "code extent")
    w = struct.unpack(f"<{len(code)//4}I", code)
    i = 0

    def word():
        nonlocal i
        need(i < len(w), "truncated instruction or missing end")
        i += 1
        return w[i-1]

    # Annotated V_NOP (VOP3, src2=literal, src0/1 VGPR) consumes three words.
    while i < len(w) and w[i] >> 16 == 0xd580:
        word()
        args = word()
        need(args >> 18 == 255 and args & 0x20100 == 0x20100, "unsupported V_NOP operands")
        word()

    regs = {}
    inline = {128: 0., 240: .5, 241: -.5, 242: 1., 243: -1., 244: 2., 245: -2.,
              246: 4., 247: -4.}
    while True:
        op = word()
        dst = op >> 17 & 255
        if op == 0xbf800000 or op == 0xbfa00003:  # S_NOP 0 / S_INST_PREFETCH 3
            continue
        if op & 0xfe01fe00 == 0x7e000200:  # V_MOV_B32 e32
            src = op & 511
            need(src in inline, "unsupported move source")
            regs[dst] = inline[src]
        elif op >> 25 == 0x2f:  # V_CVT_PKRTZ_F16_F32 e32: low, high
            a, b = op & 511, op >> 9 & 255
            need(a >= 256 and isinstance(regs.get(a-256), float) and
                 isinstance(regs.get(b), float), "pack needs two defined float VGPRs")
            # Every accepted float is exact in binary16. Track both lanes instead of rounding.
            regs[dst] = (regs[a-256], regs[b])
        elif op == 0xf8001c0f:  # EXP MRT0, RGBA, COMPR, DONE, VM
            arg = word()
            a, b = regs.get(arg & 255), regs.get(arg >> 8 & 255)
            need(arg >> 16 == 0 and isinstance(a, tuple) and isinstance(b, tuple),
                 "export needs two packed VGPRs")
            need(word() == 0xbf810000 and all(t == 0xbf9f0000 for t in w[i:]),
                 "expected S_ENDPGM and optional S_CODE_END padding")
            return ("#version 450\nlayout(location=0) out vec4 color;\n"
                    "void main(){color=vec4(" + ", ".join(map(str, a+b)) + ");}\n")
        else:
            raise ValueError(f"unsupported instruction at byte {(i-1)*4}")
