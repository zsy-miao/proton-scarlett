"""Read one explicitly named local Wrapped record into memory. Never dump native bytes."""
import mmap
import os
import struct
import zlib
from typing import NamedTuple

from native_ps import need


def span(data, pos, size):
    need(0 <= pos <= len(data) - size and size >= 0, "range outside container")
    return data[pos:pos+size]


def fields(data, pos, fmt):
    fmt = "<" + fmt
    return struct.unpack(fmt, span(data, pos, struct.calcsize(fmt)))


def inflate(data, size, mode, trailer):
    need(0 < size <= 4 << 20, "inflate limit")
    z = zlib.decompressobj(mode)
    result = z.decompress(data, size+1)
    need(z.eof and not z.unconsumed_tail and len(z.unused_data) == trailer and
         len(result) == size, "compressed extent")
    return result


class Record(NamedTuple):
    code: bytes
    inputs: bytes
    outputs: bytes
    psv: bytes
    resources: tuple = ()


def dxbc(data, kind, *, inspect_raw_vs=False):
    size, count = fields(data, 24, "2I")
    need(data[:4] == b"DXBC" and len(data) == size and 0 < count <= 32, "DXBC header")
    parts = {}
    end = 32+4*count
    for off in fields(data, 32, f"{count}I"):
        tag, n = fields(data, off, "4sI")
        need(off >= end and not off % 4 and tag not in parts, "ordered unique chunks")
        parts[tag] = span(data, off+8, n)
        end = off+8+n
    need(end == size and {b"DXIL", b"ISG1", b"OSG1", b"PSV0"} <= parts.keys(), "required chunks")
    need(kind in (0, 1), "expected PS or VS")
    psv = parts[b"PSV0"]
    resources, = fields(psv, 56, "I")
    raw_vs = inspect_raw_vs and kind == 1 and resources == 1 and fields(psv, 60, "7I") == (
        24, 4, 3, 0, 0, 11, 0)  # one ByteAddressBuffer t0 space3
    need(fields(psv, 0, "I") == (52,) and (resources==0 or (not kind and resources==2) or raw_vs),
         "PSV resources")

    x = parts[b"DXIL"]
    stage, words, magic, version, bc, n = fields(x, 0, "II4sIII")
    need(stage == kind<<16 | 0x66 and words*4 == len(x) and magic == b"DXIL" and version == 0x106 and
         span(x, 8+bc, n) == b"NOBC", "native-only SM6.6 stage")
    magic, count, table = fields(x, 24, "4sII")
    need(magic == b"XDAT" and count == 3 and table == 36, "XDAT header")
    entries = {}
    end = table + count*12
    for i in range(count):
        ident, pos, size = fields(x, table+12*i, "III")
        need(pos >= end and ident not in entries, "ordered unique XDAT entries")
        entries[ident] = span(x, pos, size)
        if ident == 7:
            k, _, tag, v, rel, size_bc = fields(entries[ident], 0, "II4sIII")
            need((k, tag, v, size_bc) == (stage, b"DXIL", version, n)
                 and pos+rel == bc and rel >= 16 and span(entries[ident], 8+rel, n) == b"NOBC",
                 "shared nested bitcode sentinel")
        end = pos + size
    need(end == len(x) and set(entries) == {7,13,18}, "single native variant")
    e = entries[13]
    magic, version, compressed, raw = fields(e, 0, "4sIQQ")
    need((magic, version, compressed) == (b"XBCH", 3, len(e)-24), "XBCH header")
    f = inflate(e[24:], raw, 15, 0)
    need(fields(f, 0, "4sIQ") == (b"FSSM", 0x101, len(f)-16), "FSSM header")
    desc = 16 + fields(f, 56 if kind else 64, "Q")[0]
    need(fields(f, desc+64, "Q")[0] == (0xc000020004 if kind else 0xc000020105),
         "native stage descriptor")
    off, n = fields(f, desc, "QQ")
    need(off % 4 == 0 and 16+off >= desc+72 and 0 < n <= 4096, "ISA extent")
    table, = fields(f, desc+48, "Q")
    need(fields(f, desc+60, "I") == (resources,), "native/PSV resource counts")
    mapping = tuple(fields(f, 16+table+20*i, "5I") for i in range(resources))
    if raw_vs:
        need(mapping == ((0x100,0,0,1,0x804),), "native raw VS resource row")
    return Record(span(f, 16+off, n), parts[b"ISG1"], parts[b"OSG1"], psv, mapping)


def read_record(path, name, kind):
    label = name.encode("ascii") + b"\0"
    need(0 < len(label) < 256, "record name")
    needle = bytes([len(label)]) + label
    with open(path, "rb") as fh:
        need(0 < os.fstat(fh.fileno()).st_size <= 1 << 30, "pack extent")
        with mmap.mmap(fh.fileno(), 0, access=mmap.ACCESS_READ) as m:
            pos = m.find(needle)
            need(pos >= 0 and m.find(needle, pos+1) == -1, "expected unique named record")
            pos += len(needle)
            need(span(m,pos,1) in (b"@", b"2"), "record header type")
            pos += 1
            n, _, duplicate, raw = fields(m, pos, "4I")
            need(0 < n == duplicate <= 4 << 20, "record extent")
            return dxbc(inflate(span(m, pos+16, n), raw, -15, 12), kind)


def read_native(path, name):
    ps = read_record(path, name, 0)
    need(not ps.resources, "expected resource-free PS")
    sig = ps.outputs
    count, header, stream, name, index, semantic, component, reg, mask, prec = fields(sig, 0, "10I")
    need((count,header,stream,index,semantic,component,reg,mask,prec) ==
         (1,8,0,0,64,3,0,15,0) and name >= 40 and span(sig, name, 10) == b"SV_Target\0",
         "expected float4 SV_Target0")
    return ps.code
