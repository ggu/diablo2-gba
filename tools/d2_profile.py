#!/usr/bin/env python3
"""Replay Den controller input on ARM; measure the GBA's hardware timer.

Only the controller queue/debug schedule is written. Gameplay and SRAM are
observed, never injected. A host-produced state hash verifies every checkpoint.
"""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import socket
import struct
import subprocess
import tempfile
import time
try:  # package import for tests; direct import for ``python tools/d2_profile.py``
    from .emulator_smoke import ROOT, RSP, symbols, symbol_sizes, capture, require_mgba
    from .gba_profile import hardware_clock, read_u32
except ImportError:
    from emulator_smoke import ROOT, RSP, symbols, symbol_sizes, capture, require_mgba
    from gba_profile import hardware_clock, read_u32

def command_ok(rsp: RSP, command: str) -> None:
    answer=rsp.cmd(command)
    if answer!='OK':raise RuntimeError(f'Debugger command failed: {command}: {answer}')

FIELDS = ('hash','screen','hp','quest','kills','px','py','skill_points')
CPU_HZ = 16_777_216

def state_offsets() -> dict[str, tuple[int, int]]:
    """Get live-field offsets from the shared C layout instead of extending den_debug."""
    names=('area','rewarded','equipment','items')
    expressions={
        'area':'offsetof(DenState,area)',
        'rewarded':'offsetof(DenState,rewarded)',
        'equipment':'offsetof(DenState,gear)+offsetof(DenItemState,equipment)',
        'items':'offsetof(DenState,gear)+offsetof(DenItemState,items)',
    }
    with tempfile.TemporaryDirectory(prefix='den-profile-layout-') as tmp:
        src=Path(tmp)/'layout.c';exe=Path(tmp)/'layout'
        src.write_text('#include <stdio.h>\n#include <stddef.h>\n#include "diablo2/den_sim.h"\nint main(void){\n'+
            ''.join(f'printf("{name} %zu %zu\\n",{expr},'+
                ('sizeof(DenItem)' if name=='items' else 'sizeof(((DenItemState*)0)->equipment)' if name=='equipment' else 'sizeof(((DenState*)0)->'+name+')')+');\n' for name,expr in expressions.items())+
            'return 0;}\n')
        subprocess.run(['clang','-O2','-DGBA_HOST','-I',str(ROOT),str(src),'-o',str(exe)],check=True)
        return {line.split()[0]:tuple(map(int,line.split()[1:]))
                for line in subprocess.check_output([str(exe)],text=True).splitlines()}

def read_extra_state(rsp: RSP, address: int, layout: dict[str, tuple[int,int]]) -> dict:
    def val(name: str) -> int:
        offset,size=layout[name]
        return int.from_bytes(rsp.read(address+offset,size),'little')
    item_offset,item_size=layout['items'];equip_offset,_=layout['equipment']
    records=rsp.read(address+item_offset,item_size*48)
    equipped=rsp.read(address+equip_offset,7)
    def base_id(slot: int) -> int:
        ref=equipped[slot]
        return records[(ref-1)*item_size] if ref else 0
    return {'area':val('area'),'rewarded':val('rewarded'),'weapon':base_id(0),
            'shield':base_id(1),'torso':base_id(3),
            'items':sum(bool(records[i*item_size]) for i in range(48))}

def run(trace_path: Path, fps_target: int=600) -> dict:
    trace=json.loads(trace_path.read_text())
    offsets=state_offsets()
    ticks=trace['total_ticks'];events=trace['events']
    rom=ROOT/'build/Diablo2-Den-of-Evil-GBA.gba';elf=ROOT/'build/diablo2-den.elf'
    sym,sizes=symbols(elf),symbol_sizes(elf)
    if len(events)>sizes['demo_schedule_events']//8:raise ValueError('Controller queue capacity exceeded')
    if not events or any(not 0<=e['tick']<ticks or not 0<=e['held']<=1023 for e in events):raise ValueError('Invalid controller queue')
    if any(a['tick']>=b['tick'] for a,b in zip(events,events[1:])):raise ValueError('Controller events must be sorted and unique')
    expected={c['tick']:c for c in trace['checkpoints']}
    out=ROOT/'build/emulator/d2-den';out.mkdir(parents=True,exist_ok=True)
    saves=out/f'saves-{time.time_ns()}';saves.mkdir()
    check=socket.socket()
    try:check.bind(('127.0.0.1',2345))
    except OSError:raise RuntimeError('mGBA debugger port 2345 already in use')
    finally:check.close()
    log=(out/'mgba.log').open('ab')
    proc=subprocess.Popen([require_mgba(),'-g','-C','mute=1','-C','audioSync=0','-C','videoSync=0','-C',f'fpsTarget={fps_target}','-C',f'savegamePath={saves}',str(rom)],stdout=log,stderr=log)
    sock=None
    try:
        deadline=time.monotonic()+12
        while time.monotonic()<deadline:
            try:sock=socket.create_connection(('127.0.0.1',2345),timeout=1);break
            except OSError:time.sleep(.1)
        if sock is None:raise RuntimeError('mGBA debugger did not start')
        sock.settimeout(40);sock.setsockopt(socket.IPPROTO_TCP,socket.TCP_NODELAY,1)
        r=RSP(sock);r.cmd('qSupported');r.cmd('?')
        entry=sym['game_init']&~1;command_ok(r,f'Z1,{entry:x},2');r.tick()
        queue=b''.join(struct.pack('<2I',e['tick'],e['held']) for e in events)
        for offset in range(0,len(queue),256):r.write(sym['demo_schedule_events']+offset,queue[offset:offset+256])
        r.write(sym['demo_schedule_length'],struct.pack('<I',ticks))
        r.write(sym['demo_schedule_count'],struct.pack('<I',len(events)))
        command_ok(r,f'z1,{entry:x},2')
        checkpoint=sym['demo_replay_checkpoint']&~1
        # Stop after the checkpoint function has published its observation.
        # Break at its caller immediately after BL, using the return LR.
        command_ok(r,f'Z1,{checkpoint:x},2')
        snapshots=[];last_clock=None;last_tick=0;total_cycles=0
        while True:
            r.tick()
            tick=read_u32(r,sym['demo_schedule_tick'])
            # game_init and each game_update already keep d current; let the
            # checkpoint's read-only observer execute before reading its output.
            registers=r.cmd('g');lr=int.from_bytes(bytes.fromhex(registers[14*8:15*8]),'little')&~1
            command_ok(r,f'z1,{checkpoint:x},2')
            command_ok(r,f'Z1,{lr:x},2');r.tick();command_ok(r,f'z1,{lr:x},2')
            state=dict(zip(FIELDS,struct.unpack('<8I',r.read(sym['den_debug'],32))))
            state.update(read_extra_state(r,sym['d'],offsets))
            state['tick']=tick
            want=expected.get(tick)
            if want:
                for key in ('tick',)+FIELDS+('area','rewarded','weapon','shield','torso','items'):
                    if key in want and state[key]!=want[key]:raise AssertionError(f'ARM diverged at {tick}: {key} {state[key]} != {want[key]}')
            else:raise AssertionError(f'No host checkpoint for ARM tick {tick}')
            now=hardware_clock(r)
            if last_clock is not None:
                elapsed=(now-last_clock)&0xffffffff;total_cycles+=elapsed
                state['interval_fps']=round(CPU_HZ*(tick-last_tick)/max(1,elapsed),2)
            state['last_cycles']=read_u32(r,sym['demo_last_cycles'])
            state['peak_cycles']=read_u32(r,sym['demo_peak_cycles'])
            state['profile']={key:read_u32(r,sym[key]) for key in ('demo_update_cycles','gfx_draw_cycles','den_terrain_cycles','den_actor_cycles')}
            snapshots.append(state)
            print(json.dumps(state),flush=True)
            if tick:capture(r,out/f'arm-tick-{tick}.png')
            if tick==ticks:break
            command_ok(r,f'Z1,{checkpoint:x},2')
            last_clock,last_tick=now,tick
        if state['screen']!=1 or state['quest']!=2 or not state['hp'] or state['area']!=0 or not state['rewarded']:
            raise AssertionError('No living-player Akara turn-in in Rogue Encampment')
        raw=r.read(0x0e000000,32768);records=[]
        for offset in (0,16384):
            tag,size,checksum,sequence=struct.unpack_from('<4I',raw,offset)
            if not 0<size<=16368 or not sequence:continue
            value=2166136261
            for byte in raw[offset+16:offset+16+size]:value=((value^byte)*16777619)&0xffffffff
            if value==checksum:records.append({'tag':hex(tag),'bytes':size,'sequence':sequence})
        if not records:raise AssertionError('No valid SRAM quest save')
        (out/'completed.sav').write_bytes(raw)
        result={'rom_sha256':hashlib.sha256(rom.read_bytes()).hexdigest(),'ticks':ticks,'events':len(events),'host_checkpoints_matched':len(snapshots),'controller_completion_verified':True,'mean_fps':round(CPU_HZ*ticks/max(1,total_cycles),2),'maximum_tick_cycles':max(s['peak_cycles'] for s in snapshots),'save_records':records,'checkpoints':snapshots}
        (out/'profile.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps({k:v for k,v in result.items() if k!='checkpoints'},indent=2))
        return result
    finally:
        if sock:sock.close()
        proc.terminate()
        try:proc.wait(timeout=3)
        except subprocess.TimeoutExpired:proc.kill();proc.wait()
        log.close()

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--trace',type=Path,default=ROOT/'build/den-completion-input.json');parser.add_argument('--fps-target',type=int,default=600);a=parser.parse_args();run(a.trace,a.fps_target)
