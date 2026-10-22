#!/usr/bin/env python3
"""Verify completed Den SRAM in a fresh mGBA process using title Load input."""
import argparse,hashlib,json,pathlib,socket,struct,subprocess,tempfile,time
try:  # package import for tests; direct import for ``python tools/d2_save_check.py``
    from .emulator_smoke import ROOT,RSP,symbols,capture,require_mgba
    from .d2_profile import state_offsets, read_extra_state
except ImportError:
    from emulator_smoke import ROOT,RSP,symbols,capture,require_mgba
    from d2_profile import state_offsets, read_extra_state


def run():
    out=ROOT/'build/emulator/d2-den';report=json.loads((out/'profile.json').read_text())
    rom=ROOT/'build/Diablo2-Den-of-Evil-GBA.gba'
    if hashlib.sha256(rom.read_bytes()).hexdigest()!=report['rom_sha256']:raise RuntimeError('Profile belongs to another ROM build')
    fields=['screen','quest','hp','kills','skill_points']
    # Ask the host C compiler for field offsets from the actual shared header.
    with tempfile.TemporaryDirectory(prefix='den-layout-') as tmp:
        tmp=pathlib.Path(tmp);src=tmp/'layout.c';exe=tmp/'layout'
        src.write_text('#include <stdio.h>\n#include <stddef.h>\n#include "diablo2/den_sim.h"\nint main(void){\n'+''.join(f'printf("{name} %zu %zu\\n",offsetof(DenState,{name}),sizeof(d.{name}));\n' for name in fields)+'printf("gear_span %zu %zu\\n",offsetof(DenState,gear),offsetof(DenState,area)-offsetof(DenState,gear));\nreturn 0;}\n')
        subprocess.run(['clang','-O2','-DGBA_HOST','-I',str(ROOT),str(src),'-o',str(exe)],check=True)
        layout={line.split()[0]:tuple(map(int,line.split()[1:])) for line in subprocess.check_output([str(exe)],text=True).splitlines()}
    extra_layout=state_offsets()
    sym=symbols(ROOT/'build/diablo2-den.elf')
    saves=out/f'reload-saves-{time.time_ns()}';saves.mkdir()
    (saves/'Diablo2-Den-of-Evil-GBA.sav').write_bytes((out/'completed.sav').read_bytes())
    check=socket.socket()
    try:check.bind(('127.0.0.1',2345))
    except OSError:raise RuntimeError('mGBA debugger already in use')
    finally:check.close()
    log=(out/'reload.log').open('ab');connection=None
    proc=subprocess.Popen([require_mgba(),'-g','-C','mute=1','-C','audioSync=0','-C','videoSync=0','-C',f'savegamePath={saves}',str(rom)],stdout=log,stderr=log)
    try:
        for _ in range(120):
            try:connection=socket.create_connection(('127.0.0.1',2345),timeout=1);break
            except OSError:time.sleep(.1)
        if connection is None:raise RuntimeError('mGBA did not start')
        connection.settimeout(30);connection.setsockopt(socket.IPPROTO_TCP,socket.TCP_NODELAY,1)
        r=RSP(connection);r.cmd('qSupported');r.cmd('?')
        address=sym['game_update']&~1
        if r.cmd(f'Z1,{address:x},2')!='OK':raise RuntimeError('Failed to set input breakpoint')
        r.tick();r.write(sym['demo_inject_keys'],struct.pack('<H',4));r.tick(2)
        r.write(sym['demo_inject_keys'],b'\0\0');r.tick(2)
        state={name:int.from_bytes(r.read(sym['d']+offset,size),'little',signed=name=='hp') for name,(offset,size) in layout.items() if name in fields}
        state.update(read_extra_state(r,sym['d'],extra_layout))
        expected={name:report['checkpoints'][-1][name] for name in fields}
        expected.update({name:report['checkpoints'][-1][name] for name in ('area','rewarded','weapon','shield','torso','items')})
        if state!=expected:raise AssertionError(f'Fresh SRAM load diverged: {state} != {expected}')
        # The journal record holds DenSave's 8-byte header before DenState.
        # Compare every serialized gear/ground-drop byte through loot_dropped.
        raw=(out/'completed.sav').read_bytes()
        valid=[]
        for offset in (0,16384):
            if offset+16>len(raw):continue
            tag,size,checksum,sequence=struct.unpack_from('<4I',raw,offset)
            if tag!=0x44454E31 or not sequence or offset+16+size>len(raw):continue
            payload=raw[offset+16:offset+16+size]
            value=2166136261
            for byte in payload:value=((value^byte)*16777619)&0xffffffff
            if value==checksum and size>=8+layout['gear_span'][0]+layout['gear_span'][1]:
                valid.append((sequence,payload))
        if not valid:raise AssertionError('No valid completed Den save payload in SRAM journal')
        sequence,payload=max(valid,key=lambda x:x[0])
        gear_offset,gear_size=layout['gear_span']
        saved_gear=payload[8+gear_offset:8+gear_offset+gear_size]
        loaded_gear=r.read(sym['d']+gear_offset,gear_size)
        if loaded_gear!=saved_gear:
            raise AssertionError(f'Fresh load gear/ground-drop bytes differ from journal sequence {sequence}')
        result={'rom_sha256':report['rom_sha256'],'fresh_process_load_verified':True,
                'inventory_ground_bytes_verified':True,'inventory_ground_bytes':gear_size,
                'journal_sequence':sequence,'state':state,'capture':capture(r,out/'fresh-load.png')}
        (out/'fresh-load.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
    finally:
        if connection:connection.close()
        proc.terminate()
        try:proc.wait(timeout=3)
        except subprocess.TimeoutExpired:proc.kill();proc.wait()
        log.close()
if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.parse_args()
    run()
