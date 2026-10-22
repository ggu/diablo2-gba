#!/usr/bin/env python3
import pathlib,sys
from emulator_smoke import png
for arg in sys.argv[1:]:
 p=pathlib.Path(arg);parts=p.read_bytes().split(b'\n',3);w,h=map(int,parts[1].split());png(p.with_suffix('.png'),parts[3],w,h)
