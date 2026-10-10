import sys,pathlib,importlib.util,io,time,socket,threading,subprocess,base64,json,select,struct
ROOT=pathlib.Path.cwd()
assert (ROOT/'tools/deploy-title.py').is_file(), 'Run from the repository root'
sys.path.insert(0,str(ROOT/'build/network-elevation'))
import console as c
spec=importlib.util.spec_from_file_location('runner',ROOT/'build/installed-paths/console_test.py');r=importlib.util.module_from_spec(spec);spec.loader.exec_module(r)
BASE=r.BASE
OUT=pathlib.Path(sys.argv[1]).resolve()
assert ROOT not in (OUT, *OUT.parents), 'Raw captures must stay outside the repository'
OUT.mkdir()
mode=sys.argv[2] if len(sys.argv)>2 else 'relay-join'
assert mode in ('relay-join', 'lan-host')
# Original diagnostic NROM: poll both controllers into RAM; increment a counter.
code=bytearray.fromhex('78 d8 a2 ff 9a a9 01 8d 16 40 a9 00 8d 16 40 a2 08 ad 16 40 4a 26 01 ad 17 40 4a 26 02 ca d0 f1 e6 00 4c 05 c0')
prg=code+bytes([0xea])*(16384-len(code));prg[-6:]=bytes.fromhex('00 c0 00 c0 00 c0')
fixture=OUT/'input-test.nes'
fixture.write_bytes(b'NES\x1a'+bytes([1,1])+bytes(10)+prg+bytes(8192))
relay='us-east1.relay.retroarch.com'; stop=threading.Event(); ready=threading.Event(); session=[]; streams=[]
listener=socket.socket();listener.bind(('127.0.0.1',0));listener.listen();listener.settimeout(.3)
proxyport=listener.getsockname()[1]
def forward(local,index):
 try:
  dest=(relay,55435) if mode=='relay-join' else (c.settings['host'],55435)
  with local, socket.create_connection(dest,10) as remote:
   remote.settimeout(.5); local.settimeout(.5)
   buffers=[bytearray(),bytearray()]; streams.append(buffers)
   while not stop.is_set():
    rr,_,_=select.select([local,remote],[],[],.5)
    for src in rr:
     data=src.recv(65536)
     if not data:return
     side=0 if src is local else 1
     buffers[side].extend(data)
     (OUT/f'wire-{index}-{side}.bin').write_bytes(buffers[side])
     if mode=='relay-join' and side==1 and len(buffers[1])>=16 and buffers[1][:4]==b'RATS' and not ready.is_set():
      session.append(base64.b64encode(buffers[1][4:16]).decode());ready.set()
     (remote if src is local else local).sendall(data)
 except (OSError,TimeoutError):pass

def accept():
 index=0
 while not stop.is_set():
  try:q,_=listener.accept()
  except socket.timeout:continue
  threading.Thread(target=forward,args=(q,index),daemon=True).start();index+=1
threading.Thread(target=accept,daemon=True).start()
common='''config_save_on_exit = "false"
video_shader_enable = "false"
video_filter = ""
cheevos_enable = "false"
netplay_public_announce = "false"
netplay_nat_traversal = "false"
netplay_password = ""
netplay_spectate_password = ""
netplay_check_frames = "60"
libretro_log_level = "0"
frontend_log_level = "0"
'''
pc=None
try:
 assert 'count=0' in c.ctl('procs'),'Title not idle'
 with c.deploy.connect(**c.settings) as ftp:
  try: marker=c.deploy.remote_bytes(ftp,BASE+'lapy-ready')
  except Exception:marker=b''
  assert not marker,'Existing daemon readiness marker; inspect first'
  last=c.deploy.remote_bytes(ftp,BASE+'tests/lapy-elevation.log')
  assert any(b'daemon_result' in line and b'root_balanced=1' in line and (b'stage=complete error=0' in line or b'stage=service_complete error=0' in line) for line in last.splitlines()),'Previous daemon not complete'
  for n in ['test-run.txt','args.txt','picker/picker-test.txt','es-de/capture-test.txt','startup-paths.log','pad-script.txt']:
   c.deploy.remove_if_present(ftp,BASE+n)
  ftp.storbinary('STOR '+BASE+'pad-script.txt',io.BytesIO(b'8 B 2\n14 A 2\n20 RIGHT 2\n30 STOP\n'))
  c.deploy.upload_atomic(ftp,fixture,BASE+'tests/netplay-test.nes')
  ftp.storbinary('STOR '+BASE+'test-run.txt',io.BytesIO(b'online feature check\n'))
  ftp.storbinary('STOR '+BASE+'tests/installed-paths.cfg',io.BytesIO((common+'video_fullscreen_x = "1920"\nvideo_fullscreen_y = "1080"\nnetplay_use_mitm_server = "false"\n').encode()))
 cfg=common+(ROOT/'build/netplay-host/pc.cfg').read_text().replace('netplay_use_mitm_server = "false"', '')
 if mode=='relay-join':cfg+='\nnetplay_use_mitm_server = "true"\nnetplay_mitm_server = "custom"\nnetplay_custom_mitm_server = "127.0.0.1|'+str(proxyport)+'"\n'
 cfg+='\nglobal_core_options = "true"\ngame_specific_options = "false"\ncontent_history_enable = "false"\ncore_options_path = "'+str(OUT/'core-options.cfg')+'"\nsavefile_directory = "'+str(OUT)+'"\nsavestate_directory = "'+str(OUT)+'"\n'
 (OUT/'pc.cfg').write_text(cfg)
 pcargs=['/usr/bin/retroarch','--config',str(OUT/'pc.cfg'),'-L',str(ROOT/'build/netplay-host/fceumm/fceumm_libretro.so'),str(fixture),'--nick=pc-online-test']
 if mode=='relay-join':
  logfile=(OUT/'pc.log').open('wb');pc=subprocess.Popen(pcargs+['--host'],stdout=logfile,stderr=subprocess.STDOUT)
  assert ready.wait(20),'Relay session not ready'
  endpoint=relay+'|55435|'+session[0]
  netarg='--connect='+endpoint
  print('Private relay session ready; no lobby announcement',flush=True)
 else:netarg='--host'
 args=['--appendconfig=/app0/tests/installed-paths.cfg','--max-frames=6000','-L','/app0/cores/fceumm_libretro.so','/app0/tests/netplay-test.nes',netarg,'--port=55435','--nick=ps5-online-test']
 with c.deploy.connect(**c.settings) as ftp:ftp.storbinary('STOR '+BASE+'args.txt',io.BytesIO(('\n'.join(args)+'\n').encode()))
 r.start_daemon(OUT,ROOT/'build/network-elevation/lapy/build/owned_root_daemon/lapy-root-daemon.elf')
 (OUT/'launch.txt').write_text(c.ctl('launch PPSA99169'))
 if mode!='relay-join':
  time.sleep(6)
  logfile=(OUT/'pc.log').open('wb');pc=subprocess.Popen(pcargs+['--connect=127.0.0.1','--port='+str(proxyport)],stdout=logfile,stderr=subprocess.STDOUT)
 print('Session running for 35 seconds',flush=True);time.sleep(35)
 pc.terminate()
 try:pc.wait(timeout=3)
 except subprocess.TimeoutExpired:pc.kill();pc.wait()
 # Close the peer first, allowing the title to release its network descriptors.
 time.sleep(1)
 if 'count=0' not in c.ctl('procs'):
  (OUT/'close.txt').write_text(c.ctl('kill PPSA99169'))
 time.sleep(2)
 for name in ['retroarch.log','trace.txt','startup-paths.log','tests/lapy-elevation.log']:r.fetch(name,OUT)
 for _ in range(120):
  data=r.fetch('tests/lapy-elevation.log',OUT)
  if b'daemon_result' in data:break
  time.sleep(.5)
 print('Console idle='+str('count=0' in c.ctl('procs')),flush=True)
 print('Daemon balanced='+str(b'root_balanced=1' in data),flush=True)
 for name in ['pc.log','retroarch.log']:
  log=(OUT/name).read_text(errors='replace')
  print(name, 'connected='+str('Connected to:' in log or 'has joined as player 2' in log),'netplay_errors='+str(sum('[Netplay]' in line and '[ERROR]' in line for line in log.splitlines())),flush=True)
finally:
 stop.set()
 # Stop only this test title if an intermediate assertion failed after launch.
 if (OUT/'launch.txt').exists() and 'count=0' not in c.ctl('procs'):
  (OUT/'cleanup-close.txt').write_text(c.ctl('kill PPSA99169'))
 if pc:
  pc.terminate()
  try:pc.wait(timeout=3)
  except subprocess.TimeoutExpired:pc.kill();pc.wait()
 with c.deploy.connect(**c.settings) as ftp:
  for n in ['test-run.txt','args.txt','tests/installed-paths.cfg','pad-script.txt']:c.deploy.remove_if_present(ftp,BASE+n)
