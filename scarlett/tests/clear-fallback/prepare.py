"""Generate a local read-only image fragment for the exact tested title; never ship it."""
import hashlib, pathlib, sys, struct
exe=pathlib.Path(sys.argv[1]).read_bytes()
if hashlib.sha256(exe).hexdigest()!='e95020b2fa4a721db3df59fdecf75621730ac559d8835252f6c3b658d5a08993':
    raise SystemExit('wrong title build')
text=[(0x122630,0x122cd1),(0x555e570,0x555e5f9),(0x4d9f3e0,0x4d9f4e4),(0x4d9d990,0x4d9d9e5),
      (0x49a0,0x49a5),(0x4f80240,0x4f802bc),(0x4da9f50,0x4da9f90),(0x517acc0,0x517acce),
      (0xbfa70,0xbffc9),(0x32130,0x33cb5),(0x4e34010,0x4e3402e)]
with open(sys.argv[2],'wb') as f:
    for bias,ranges in [(0xc00,text),(0x1200,[(0x6159bc0,0x615c3b0),(0x61ecf30,0x61ecf50)])]:
        for a,b in ranges:
            f.write(struct.pack('<II',a,b-a)); f.write(exe[a-bias:b-bias])
