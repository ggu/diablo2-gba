#!/usr/bin/env python3
"""Compare host-rendered Den checkpoints with PNG captures from mGBA."""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import tempfile
import zlib

ROOT = Path(__file__).resolve().parents[1]
W, H = 240, 160

def decode_png(path: Path) -> bytes:
    data=path.read_bytes()
    if data[:8] != b'\x89PNG\r\n\x1a\n': raise ValueError(f'{path}: invalid PNG signature')
    pos=8; width=height=bit_depth=color_type=interlace=None; packed=bytearray()
    while pos<len(data):
        n=struct.unpack_from('>I',data,pos)[0];kind=data[pos+4:pos+8];body=data[pos+8:pos+8+n];pos+=12+n
        if kind==b'IHDR': width,height,bit_depth,color_type,compression,filter_method,interlace=struct.unpack('>IIBBBBB',body)
        elif kind==b'IDAT': packed.extend(body)
        elif kind==b'IEND': break
    if (width,height,bit_depth,color_type,interlace)!=(W,H,8,2,0): raise ValueError(f'{path}: expected non-interlaced RGB8 240x160 PNG')
    raw=zlib.decompress(packed); stride=W*3
    if len(raw)!=(stride+1)*H: raise ValueError(f'{path}: invalid scanline data')
    out=bytearray(stride*H);prev=bytearray(stride);offset=0
    for y in range(H):
        filt=raw[offset];offset+=1;row=bytearray(raw[offset:offset+stride]);offset+=stride
        for x in range(stride):
            a=row[x-3] if x>=3 else 0;b=prev[x];c=prev[x-3] if x>=3 else 0
            if filt==1: row[x]=(row[x]+a)&255
            elif filt==2: row[x]=(row[x]+b)&255
            elif filt==3: row[x]=(row[x]+((a+b)//2))&255
            elif filt==4:
                p=a+b-c;pa=abs(p-a);pb=abs(p-b);pc=abs(p-c);q=a if pa<=pb and pa<=pc else b if pb<=pc else c
                row[x]=(row[x]+q)&255
            elif filt!=0: raise ValueError(f'{path}: unsupported PNG filter {filt}')
        out[y*stride:(y+1)*stride]=row;prev=row
    return bytes(out)

def decode_ppm(path: Path) -> bytes:
    data=path.read_bytes();parts=data.split(b'\n',3)
    if len(parts)!=4 or parts[0]!=b'P6' or parts[1]!=b'240 160' or parts[2]!=b'255' or len(parts[3])!=W*H*3:
        raise ValueError(f'{path}: invalid host PPM')
    return parts[3]

def run(trace_path: Path, arm_dir: Path, out_dir: Path) -> dict:
    trace=json.loads(trace_path.read_text());ticks=int(trace['total_ticks'])
    out_dir.mkdir(parents=True,exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='d2-den-visual-') as tmp:
        tmp=Path(tmp);events=tmp/'events.txt'
        events.write_text(''.join(f"{e['tick']} {e['held']}\n" for e in trace['events']))
        driver=tmp/'den-visual-replay'
        cmd=['clang','-std=c11','-O1','-g','-fsanitize=address,undefined','-DGBA_HOST','-DD2_DEN_OF_EVIL','-Icommon','-I.local/d2/generated',
             'tests/den_visual_replay.c','diablo2/den_game.c','common/gba.c','-o',str(driver)]
        subprocess.run(cmd,cwd=ROOT,check=True)
        subprocess.run([str(driver),str(events),str(out_dir),str(ticks)],cwd=ROOT,check=True)
    # At tick zero no ARM frame has been presented yet. State is still hash-checked.
    checkpoints=sorted(({int(c['tick']) for c in trace['checkpoints']}|{ticks})-{0})
    results=[]
    for tick in checkpoints:
        host=decode_ppm(out_dir/f'host-tick-{tick}.ppm')
        arm_path=arm_dir/f'arm-tick-{tick}.png'
        if not arm_path.exists(): raise FileNotFoundError(f'missing emulator capture {arm_path}')
        arm=decode_png(arm_path);diff=sum(a!=b for a,b in zip(host,arm))
        results.append({'tick':tick,'match':diff==0,'different_channel_values':diff,
                        'host_sha256':hashlib.sha256(host).hexdigest(),'arm_sha256':hashlib.sha256(arm).hexdigest()})
    report={'trace':str(trace_path),'arm_dir':str(arm_dir),'checkpoints':results,'pixel_exact':all(x['match'] for x in results)}
    (out_dir/'visual-compare.json').write_text(json.dumps(report,indent=2)+'\n')
    if not report['pixel_exact']: raise SystemExit('Den visual replay differs; see visual-compare.json')
    print(json.dumps(report,indent=2));return report

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--trace',type=Path,default=ROOT/'build/den-completion-input.json')
    p.add_argument('--arm-dir',type=Path,default=ROOT/'build/emulator/d2-den');p.add_argument('--out-dir',type=Path,default=ROOT/'build/emulator/d2-den/host-visual')
    a=p.parse_args();run(a.trace,a.arm_dir,a.out_dir)
