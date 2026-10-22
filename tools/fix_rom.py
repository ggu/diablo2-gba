#!/usr/bin/env python3
"""Write the standard GBA boot header and identify the cartridge as SRAM homebrew."""
import sys, pathlib
p=pathlib.Path(sys.argv[1]); title=sys.argv[2]; code=sys.argv[3]
a=bytearray(p.read_bytes())
logo=bytes.fromhex('24ffae51699aa2213d84820a84e409ad11248b98c0817f21a352be199309ce2010464a4af82731ec58c7e83382e3cebf85f4df94ce4b09c194568ac01372a7fc9f844d73a3ca9a615897a327fc039876231dc7610304ae56bf38840040a70efdff52fe036f9530f197fbc08560d68025a963be03014e38e2f9a234ffbb3e0344780090cb88113a9465c07c6387f03cafd625e48b380aac7221d4f807')
assert len(logo)==156
assert len(a)>=192
assert len(title)<=12 and len(code)==4
a[4:160]=logo
a[160:172]=title.encode().ljust(12,b' ')
a[172:176]=code.encode();a[176:178]=b'00';a[178]=0x96
a[179:189]=bytes(10);a[189]=(-sum(a[160:189])-0x19)&255;a[190:192]=b'\0\0'
size=65536
while size<len(a): size*=2
a+=b'\xff'*(size-len(a))
assert len(a)<=32*1024*1024
p.write_bytes(a)
