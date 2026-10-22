#!/usr/bin/env python3
"""Execute the real ARM ROM in mGBA, drive input at game ticks, capture video RAM."""
import argparse,hashlib,json,os,pathlib,shutil,socket,struct,subprocess,time,zlib,binascii
ROOT=pathlib.Path(__file__).resolve().parents[1]
def find_mgba():
 configured=os.environ.get('MGBA')
 if configured:
  found=shutil.which(configured) or configured
  if pathlib.Path(found).is_file() and os.access(found,os.X_OK):return found
  raise RuntimeError(f'MGBA is set but does not name an executable: {configured}')
 for name in ('mGBA','mgba'):
  found=shutil.which(name)
  if found:return found
 for fallback in (pathlib.Path.home()/'Applications/mGBA.app/Contents/MacOS/mGBA',
                  pathlib.Path('/Applications/mGBA.app/Contents/MacOS/mGBA')):
  if fallback.is_file() and os.access(fallback,os.X_OK):return str(fallback)
 return None
def require_mgba():
 mgba=find_mgba()
 if not mgba:raise RuntimeError('mGBA was not found; install it or set MGBA to its executable path')
 return mgba
class RSP:
 def __init__(self,s):self.s=s;self.buf=b''
 def packet(self,msg):
  b=msg.encode();self.s.sendall(b'$'+b+b'#'+f'{sum(b)&255:02x}'.encode())
 def recv(self):
  while True:
   if b'$' in self.buf:
    pos=self.buf.index(b'$');end=self.buf.find(b'#',pos)
    if end>=0 and len(self.buf)>=end+3:
     payload=self.buf[pos+1:end];self.buf=self.buf[end+3:];self.s.sendall(b'+');return payload.decode()
   chunk=self.s.recv(65536)
   if not chunk:raise RuntimeError('mGBA disconnected')
   self.buf+=chunk
 def cmd(self,msg):self.packet(msg);return self.recv()
 def read(self,addr,n):
  result=bytearray()
  while len(result)<n:
   k=min(512,n-len(result));answer=self.cmd(f'm{addr+len(result):x},{k:x}')
   if answer.startswith('E'):raise RuntimeError(answer)
   result+=bytes.fromhex(answer)
  return bytes(result)
 def write(self,addr,data):
  answer=self.cmd(f'M{addr:x},{len(data):x}:{data.hex()}')
  if answer!='OK':raise RuntimeError(answer)
 def tick(self,n=1):
  for _ in range(n):
   answer=self.cmd('c')
   if not answer.startswith(('T05','S05')):raise RuntimeError(f'Unexpected execution stop: {answer}')
def symbols(elf):
 out=subprocess.check_output(['arm-none-eabi-nm','-n',str(elf)],text=True);result={}
 for line in out.splitlines():
  a=line.split()
  if len(a)==3:result[a[2]]=int(a[0],16)
 return result
def symbol_sizes(elf):
 out=subprocess.check_output(['arm-none-eabi-nm','-S','-n',str(elf)],text=True);result={}
 for line in out.splitlines():
  a=line.split()
  if len(a)==4:result[a[3]]=int(a[1],16)
 return result
def png(path,pixels,w=240,h=160):
 def chunk(t,d):return struct.pack('>I',len(d))+t+d+struct.pack('>I',binascii.crc32(t+d)&0xffffffff)
 scan=b''.join(b'\0'+pixels[y*w*3:(y+1)*w*3] for y in range(h))
 path.write_bytes(b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',w,h,8,2,0,0,0))+chunk(b'IDAT',zlib.compress(scan))+chunk(b'IEND',b''))
def capture(rsp,path):
 disp=int.from_bytes(rsp.read(0x04000000,2),'little');pal=struct.unpack('<256H',rsp.read(0x05000000,512));vram=rsp.read(0x0600a000 if disp&16 else 0x06000000,38400)
 rgb=bytearray()
 for idx in vram:
  c=pal[idx];rgb.extend(((c&31)*255//31,((c>>5)&31)*255//31,((c>>10)&31)*255//31))
 png(path,rgb)
 return {'dispcnt':disp,'nonblack_pixels':sum(any(rgb[i:i+3]) for i in range(0,len(rgb),3)),'palette_indices':len(set(vram))}
def run(name,steps=60,load=False,save=False,hold_a=False):
 if name not in ('diablo2','den'):
  raise ValueError(f"unknown D2 profile: {name}")
 stem={'diablo2':'Diablo2-GBA','den':'Diablo2-Den-of-Evil-GBA'}[name]
 elf_name={'diablo2':'diablo2','den':'diablo2-den'}[name]
 rom=ROOT/'build'/f'{stem}.gba';elf=ROOT/'build'/f'{elf_name}.elf';sym=symbols(elf)
 mgba=require_mgba()
 out=ROOT/'build'/'emulator';out.mkdir(exist_ok=True)
 # Do not attach to or stop someone else's debugger.
 check=socket.socket()
 try:check.bind(('127.0.0.1',2345))
 except OSError:raise RuntimeError('Port 2345 already in use; close the existing mGBA debugger before testing')
 finally:check.close()
 save_dir=out/'saves'/name;save_dir.mkdir(parents=True,exist_ok=True)
 log=(out/f'{name}.log').open('ab')
 proc=subprocess.Popen([mgba,'-g','-C','mute=1','-C','audioSync=0','-C','videoSync=0','-C',f'savegamePath={save_dir}',str(rom)],stdout=log,stderr=log)
 s=None
 try:
  deadline=time.monotonic()+12
  while time.monotonic()<deadline:
   try:s=socket.create_connection(('127.0.0.1',2345),timeout=1);break
   except OSError:time.sleep(.1)
  if s is None:raise RuntimeError('mGBA GDB server did not start')
  s.settimeout(20);s.setsockopt(socket.IPPROTO_TCP,socket.TCP_NODELAY,1);r=RSP(s);r.cmd('qSupported');r.cmd('?')
  address=sym['game_update']&~1
  assert r.cmd(f'Z1,{address:x},2')=='OK'
  r.tick(2)
  result={'game':name,'title':capture(r,out/f'{name}-title.png')}
  start_key=4 if load else 1
  r.write(sym['demo_inject_keys'],struct.pack('<H',start_key));r.tick();r.write(sym['demo_inject_keys'],b'\0\0');r.tick()
  if hold_a:r.write(sym['demo_inject_keys'],struct.pack('<H',1))
  tick_cycles=[]
  profile_names=[k for k in ('demo_update_cycles','gfx_draw_cycles','den_terrain_cycles','den_actor_cycles') if k in sym]
  profile={k:[] for k in profile_names}
  for _ in range(steps):
   r.tick()
   tick_cycles.append(int.from_bytes(r.read(sym['demo_last_cycles'],4),'little'))
   for k in profile_names:profile[k].append(int.from_bytes(r.read(sym[k],4),'little'))
  result['play']=capture(r,out/(f'{name}-loaded.png' if load else f'{name}-play.png'))
  result['loaded_saved_game']=load
  result['held_a']=hold_a
  result['rom_sha256']=hashlib.sha256(rom.read_bytes()).hexdigest()
  result['draw_cycles']=int.from_bytes(r.read(sym['gfx_draw_cycles'],4),'little')
  result['last_tick_cycles']=int.from_bytes(r.read(sym['demo_last_cycles'],4),'little')
  result['peak_tick_cycles']=int.from_bytes(r.read(sym['demo_peak_cycles'],4),'little')
  ordered=sorted(tick_cycles)
  mean_cycles=sum(tick_cycles)/max(1,len(tick_cycles))
  result['sampled_ticks']=len(tick_cycles)
  result['mean_tick_cycles']=round(mean_cycles)
  result['p95_tick_cycles']=ordered[min(len(ordered)-1,len(ordered)*95//100)] if ordered else 0
  result['max_sampled_tick_cycles']=max(tick_cycles,default=0)
  result['effective_fps']=round(16777216/max(1,mean_cycles),2)
  result['profile_cycles']={k:{'mean':round(sum(v)/max(1,len(v))),'p95':sorted(v)[min(len(v)-1,len(v)*95//100)] if v else 0,'max':max(v,default=0)} for k,v in profile.items()}
  result['frames']=int.from_bytes(r.read(sym['demo_frame'],4),'little')
  if save:
   r.write(sym['demo_inject_keys'],b'\0\0');r.tick()
   def press(key):
    r.write(sym['demo_inject_keys'],struct.pack('<H',key));r.tick(2)
    r.write(sym['demo_inject_keys'],b'\0\0');r.tick(2)
   press(8)
   capture(r,out/f'{name}-pause.png')
   press(1)
   r.tick(60)
   records=[]
   for offset in (0,16384):
    header=r.read(0x0e000000+offset,16)
    tag,size,checksum,sequence=struct.unpack('<4I',header)
    if tag and 0<size<=16368 and sequence:
     payload=r.read(0x0e000000+offset+16,size);h=2166136261
     for v in payload:h=((h^v)*16777619)&0xffffffff
     if h==checksum:records.append({'tag':hex(tag),'bytes':size,'sequence':sequence})
   assert records,'No valid save record found after save input'
   result['save_records']=records
  result['input_key_symbol']=hex(sym['demo_inject_keys'])
  result['update_breakpoint']=hex(address)
  assert result['frames']>=steps
  assert result['play']['palette_indices']>=4
  (out/(f'{name}-loaded.json' if load else f'{name}.json')).write_text(json.dumps(result,indent=2)+'\n')
  print(json.dumps(result,indent=2))
 finally:
  if s:s.close()
  proc.terminate()
  try:proc.wait(timeout=3)
  except subprocess.TimeoutExpired:proc.kill();proc.wait()
  log.close()
if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('game',nargs='?',choices=['diablo2','den'],default='diablo2');p.add_argument('--frames',type=int,default=60);p.add_argument('--load',action='store_true');p.add_argument('--save',action='store_true');p.add_argument('--hold-a',action='store_true');a=p.parse_args();run(a.game,a.frames,a.load,a.save,a.hold_a)
