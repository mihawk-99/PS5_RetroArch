import hashlib
import io
import json
import pathlib
import socket
import sys
import threading
import time
import console as c

mode = sys.argv[1]
assert mode in ('baseline', 'elevate')
folder = c.OUT/mode
folder.mkdir(exist_ok=False)
state = c.ctl('procs')
if 'count=0' not in state: raise RuntimeError('Console not idle: '+state)
stop = threading.Event()
listener = socket.socket()
listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
listener.bind(('0.0.0.0', 19335))
listener.listen(16)
listener.settimeout(.5)
def serve():
    count = 0
    while not stop.is_set():
        try:
            q, _ = listener.accept()
            q.close(); count += 1
            (folder/'lan-accepts.json').write_text(json.dumps({'connections':count}))
        except socket.timeout: pass
threading.Thread(target=serve, daemon=True).start()
def klog():
    try:
        with socket.create_connection((c.settings['host'], 3232), 5) as q, (folder/'kernel.log').open('wb') as f:
            q.settimeout(.5)
            while not stop.is_set():
                try:
                    data=q.recv(65536)
                    if not data: break
                    f.write(data); f.flush()
                except socket.timeout: pass
    except Exception as e: (folder/'klog-error.txt').write_text(type(e).__name__)
threading.Thread(target=klog, daemon=True).start()
endpoints_path=c.OUT/'endpoints.json'
if endpoints_path.exists(): ep=json.loads(endpoints_path.read_text())
else:
    with socket.create_connection((c.settings['host'],9111),3) as q: lan=q.getsockname()[0]
    ep={'lan':lan,'port':19335,'achievements':socket.gethostbyname('retroachievements.org'),
        'media':socket.gethostbyname('media.retroachievements.org')}
    endpoints_path.write_text(json.dumps(ep,indent=2)+'\n')
control=f"{int(mode=='elevate')} {ep['lan']} {ep['port']} {ep['achievements']} {ep['media']}\n".encode()
with c.deploy.connect(**c.settings) as ftp:
    ftp.storbinary('STOR /data/homebrew/PPSA99169/tests/network-elevation.control',io.BytesIO(control))
    if c.deploy.remote_bytes(ftp,'/data/homebrew/PPSA99169/tests/network-elevation.control') != control: raise RuntimeError('Control mismatch')
    if mode == 'baseline':
        local=c.ROOT/'dist/PPSA99169/eboot.bin'
        c.deploy.upload_atomic(ftp,local,'/data/homebrew/PPSA99169/eboot.bin')
        size,digest=c.deploy.stored_digest(c.settings,'/data/homebrew/PPSA99169/eboot.bin')
        if digest!=hashlib.sha256(local.read_bytes()).hexdigest(): raise RuntimeError('Executable mismatch')
        (c.OUT/'probe-build.json').write_text(json.dumps({'size':size,'sha256':digest},indent=2)+'\n')
if mode == 'elevate':
    c.payload(c.ROOT/'build/network-elevation/lapy/build/owned_root_daemon/lapy-root-daemon.elf')
    data = ''
    for _ in range(20):
        time.sleep(.5)
        try: data=c.fetch('/data/homebrew/PPSA99169/tests/lapy-elevation.log',folder/'lapy-start.log').decode()
        except Exception: continue
        if any(x in data for x in ('phase=baseline', 'daemon_result', 'daemon_held')): break
    if 'phase=baseline' not in data or 'daemon_result' in data or 'daemon_held' in data:
        raise RuntimeError('Daemon preflight did not pass; see lapy-start.log')
response=c.ctl('launch PPSA99169')
(folder/'launch.txt').write_text(response); print(response.strip(),flush=True)
seen=set()
deadline=time.monotonic()+65
while time.monotonic()<deadline:
    time.sleep(.4)
    try: text=c.fetch('/data/homebrew/PPSA99169/tests/network-elevation.log',folder/'probe.log').decode()
    except Exception: continue
    for phase in ('before','after'):
        if f'phase={phase} snapshot' in text and phase not in seen:
            c.payload(c.ROOT/'build/network-elevation/inventory.elf');time.sleep(.5)
            c.fetch('/data/homebrew/PPSA99169/tests/privilege-inventory.txt',folder/f'inventory-{phase}.txt')
            seen.add(phase);print('Captured '+phase+' privileges',flush=True)
    if 'probe_complete' in text: break
else: raise RuntimeError('No probe completion; inspect console before retry')
time.sleep(2)
state=c.ctl('procs');(folder/'final-procs.txt').write_text(state);print(state.strip(),flush=True)
if mode=='elevate':
    c.fetch('/data/homebrew/PPSA99169/tests/lapy-elevation.log',folder/'lapy.log')
stop.set();listener.close()
for line in text.splitlines():
    if any(x in line for x in ('snapshot','connected=','connect=','elevation','complete')): print(line)
