# Console evidence fixture; prepare as described in README.txt.
# Copyright (C) 2026 Mihawk
# SPDX-License-Identifier: GPL-3.0-or-later
import sys,pathlib,time,hashlib,json,io,socket,threading,subprocess
ROOT=pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'build/network-elevation'))
import console as c
BASE='/data/homebrew/PPSA99169/'
OUT=ROOT/'klog/installed-paths'

def fetch(name,folder):
 try: return c.fetch(BASE+name,folder/name.replace('/','_'))
 except Exception as e:
  (folder/(name.replace('/','_')+'.error')).write_text(type(e).__name__)
  return b''

def deploy():
 assert 'count=0' in c.ctl('procs')
 manifest={}
 with c.deploy.connect(**c.settings) as ftp:
  for name in ['eboot.bin','picker/picker.bin','es-de/es-de.bin']:
   p=ROOT/'dist/PPSA99169'/name
   c.deploy.upload_atomic(ftp,p,BASE+name)
   size,digest=c.deploy.stored_digest(c.settings,BASE+name)
   assert digest==hashlib.sha256(p.read_bytes()).hexdigest()
   manifest[name]={'size':size,'sha256':digest}
 (OUT/'deployed.json').write_text(json.dumps(manifest,indent=2)+'\n')
 print('Three executables deployed and read-back verified',flush=True)

def start_daemon(folder,daemon):
 # Call only after inspecting the process snapshot: do not start a second daemon.
 with c.deploy.connect(**c.settings) as ftp:
  c.deploy.remove_if_present(ftp,BASE+'tests/lapy-elevation.log')
 c.payload(daemon)
 for _ in range(30):
  time.sleep(.2)
  data=fetch('tests/lapy-elevation.log',folder)
  if b'phase=baseline' in data or b'phase=service_baseline' in data:
   ready=fetch('lapy-ready',folder)
   if len(ready.split()) == 2:
    assert b'hold=73 use=72' in data, 'Reference baseline changed; stop'
    print('Daemon preflight, reference baseline 73/72 and readiness passed',flush=True);return
  if b'daemon_result' in data or b'daemon_held' in data:break
 raise RuntimeError('Daemon preflight did not pass')

def run(label,seconds=35,daemon=True,picker=None,normal=False):
 folder=OUT/label;folder.mkdir(exist_ok=False)
 choice=fetch('config/frontend.cfg',folder)
 assert 'count=0' in c.ctl('procs')
 if daemon:
  binary='owned_root_daemon-service-verified-client-max2' if picker=='retroarch' else 'owned_root_daemon'
  start_daemon(folder, ROOT/'build/network-elevation/lapy/build'/binary/'lapy-root-daemon.elf')
 with c.deploy.connect(**c.settings) as ftp:
  for name in ['test-run.txt','args.txt','picker/picker-test.txt','es-de/capture-test.txt','startup-paths.log']:
   c.deploy.remove_if_present(ftp,BASE+name)
  if not normal:
   ftp.storbinary('STOR '+BASE+'test-run.txt',io.BytesIO(b'installed paths test\n'))
  if picker=='es-de':
   ftp.storbinary('STOR '+BASE+'es-de/capture-test.txt',io.BytesIO(('12 '+label+'\n').encode()))
  if picker:
   ftp.storbinary('STOR '+BASE+'picker/picker-test.txt',io.BytesIO(('120 '+picker+'\n').encode()))
  ftp.storbinary('STOR '+BASE+'tests/installed-paths.cfg',io.BytesIO(b'config_save_on_exit = \"false\"\nvideo_fullscreen_x = \"1920\"\nvideo_fullscreen_y = \"1080\"\naspect_ratio_index = \"1\"\ncheevos_hardcore_mode_enable = \"false\"\n'))
  args=['--appendconfig=/app0/tests/installed-paths.cfg','--max-frames=1200','--ps5-capture=600']
  if label.startswith('netplay-'):
   c.deploy.upload_atomic(ftp,ROOT/'build/netplay-host/netplay-test.nes',BASE+'tests/netplay-test.nes')
   cfg=b'config_save_on_exit = \"false\"\nvideo_fullscreen_x = \"1920\"\nvideo_fullscreen_y = \"1080\"\nvideo_shader_enable = \"false\"\nvideo_filter = \"\"\ncheevos_enable = \"false\"\nnetplay_public_announce = \"false\"\nnetplay_nat_traversal = \"false\"\nnetplay_use_mitm_server = \"false\"\nnetplay_password = \"\"\nnetplay_spectate_password = \"\"\n'
   ftp.storbinary('STOR '+BASE+'tests/installed-paths.cfg',io.BytesIO(cfg))
   args=['--appendconfig=/app0/tests/installed-paths.cfg','--max-frames=3600','-L','/app0/cores/fceumm_libretro.so','/app0/tests/netplay-test.nes','--host','--port=55435','--nick=ps5-path-test']
   if label.startswith('netplay-join'):
    with socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as q:
     q.connect((c.settings['host'],55435));host=q.getsockname()[0]
    args[args.index('--host')]='--connect='+host
  if label.startswith(('achievements', 'badge-download')):
   cfg=b'config_save_on_exit = \"false\"\nvideo_fullscreen_x = \"1920\"\nvideo_fullscreen_y = \"1080\"\naspect_ratio_index = \"1\"\ncheevos_hardcore_mode_enable = \"false\"\ncheevos_enable = \"true\"\ncheevos_badges_enable = \"true\"\nthumbnails_directory = \"/app0/tests/installed-paths-media\"\n'
   if label.startswith('badge-download'):
    cfg+=b'cheevos_username = "ps5-path-download-test"\ncheevos_password = ""\ncheevos_token = ""\n'
    c.deploy.remove_if_present(ftp,BASE+'tests/installed-paths-media/cheevos/badges/00000.png')
    try:
     c.deploy.remote_bytes(ftp,BASE+'tests/installed-paths-media/cheevos/badges/00000.png')
    except Exception:
     (folder/'badge-before.json').write_text('{"absent":true}\n')
    else:raise RuntimeError('Fresh badge deletion not verified')
   ftp.storbinary('STOR '+BASE+'tests/installed-paths.cfg',io.BytesIO(cfg))
   game=json.loads((OUT/'achievement-game.json').read_text())
   assert game['path']
   args+=['-L','/app0/cores/'+game['core']+'_libretro.so',game['path']]
  if not normal:
   ftp.storbinary('STOR '+BASE+'args.txt',io.BytesIO(('\n'.join(args)+'\n').encode()))
 stop=threading.Event()
 def klog():
  with socket.create_connection((c.settings['host'],3232),5) as q, (folder/'kernel.log').open('wb') as f:
   q.settimeout(.5)
   while not stop.is_set():
    try:
     data=q.recv(65536)
     if not data:break
     f.write(data);f.flush()
    except socket.timeout:pass
 thread=threading.Thread(target=klog);thread.start()
 client=None;client_log=None
 if label.startswith('netplay-join'):
  client_log=(folder/'pc-server.log').open('wb')
  client=subprocess.Popen(['/usr/bin/retroarch','--config',str(ROOT/'build/netplay-host/pc.cfg'),'-L',str(ROOT/'build/netplay-host/fceumm/fceumm_libretro.so'),str(ROOT/'build/netplay-host/netplay-test.nes'),'--host','--port=55435','--nick=pc-path-test','--max-frames=3600'],stdout=client_log,stderr=subprocess.STDOUT)
  time.sleep(1)
 (folder/'launch.txt').write_text(c.ctl('launch PPSA99169'))
 started=time.monotonic()
 while time.monotonic()-started < seconds:
  time.sleep(1)
  elapsed=time.monotonic()-started
  if client is None and elapsed>=8 and label.startswith('netplay-host'):
   client_log=(folder/'pc-client.log').open('wb')
   client=subprocess.Popen(['/usr/bin/retroarch','--config',str(ROOT/'build/netplay-host/pc.cfg'),'-L',str(ROOT/'build/netplay-host/fceumm/fceumm_libretro.so'),str(ROOT/'build/netplay-host/netplay-test.nes'),'--connect='+c.settings['host'],'--port=55435','--nick=pc-path-test','--max-frames=3000'],stdout=client_log,stderr=subprocess.STDOUT)
  if elapsed>4 and 'count=0' in c.ctl('procs'):break
 if client:
  if client.poll() is None:
   client.terminate()
   try:client.wait(timeout=3)
   except subprocess.TimeoutExpired:client.kill();client.wait()
  client_log.close()
 state=c.ctl('procs');(folder/'final-procs.txt').write_text(state)
 if 'count=0' not in state:
  (folder/'close.txt').write_text(c.ctl('kill PPSA99169'));time.sleep(2)
 for name in ['startup-paths.log','trace.txt','retroarch.log','tests/lapy-elevation.log','picker/picker-test.jsonl','es-de/es-de-ps5.log','es-de/stdout.txt','shot.ppm']:
  fetch(name,folder)
 for _ in range(120 if daemon else 0):
  data=fetch('tests/lapy-elevation.log',folder)
  if b'daemon_result' in data or b'daemon_held' in data:break
  time.sleep(.5)
 if picker=='es-de':
  records=fetch('es-de/capture-test.jsonl',folder)
  for line in records.decode(errors='replace').splitlines():
   row=json.loads(line)
   if row.get('run')==label and row.get('written'):
    fetch(row['file'].replace('/app0/','',1),folder)
  with c.deploy.connect(**c.settings) as ftp:
   c.deploy.remove_if_present(ftp,BASE+'es-de/capture-test.txt')
 stop.set();thread.join(timeout=3)
 if picker and choice:
  with c.deploy.connect(**c.settings) as ftp:
   current=c.deploy.remote_bytes(ftp,BASE+'config/frontend.cfg')
   if current != choice:ftp.storbinary('STOR '+BASE+'config/frontend.cfg',io.BytesIO(choice))
 print('Run captured; idle='+str('count=0' in c.ctl('procs')),flush=True)
 print((folder/'startup-paths.log').read_text(),flush=True)
 print('See ignored captures for frontend and daemon results',flush=True)

if __name__=='__main__':
 if sys.argv[1]=='deploy':deploy()
 elif sys.argv[1]=='run':run(sys.argv[2],seconds=40 if sys.argv[2].startswith('netplay-') else 35)
 elif sys.argv[1]=='offline':run(sys.argv[2],seconds=25,daemon=False,normal=True)
 elif sys.argv[1]=='picker':run(sys.argv[2],seconds=35,daemon=sys.argv[4]=='online',picker=sys.argv[3])
