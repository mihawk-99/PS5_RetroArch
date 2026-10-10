import importlib.util
import pathlib
import socket
import sys
import urllib.request
import time

ROOT = pathlib.Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('deploy', ROOT / 'tools/deploy-title.py')
deploy = importlib.util.module_from_spec(spec)
spec.loader.exec_module(deploy)
settings = deploy.load_settings()
OUT = ROOT / 'klog/network-elevation'

def ctl(command):
    with socket.create_connection((settings['host'], 9111), 5) as q:
        q.settimeout(30)
        q.sendall((command+'\n').encode())
        return q.recv(16384).decode(errors='replace')

def payload(path):
    boundary = 'RetroArchPrivilegeReview'
    body = b''
    for name, data in [('args', b''), ('pipe', b'0'), ('elf', pathlib.Path(path).read_bytes())]:
        h = '--'+boundary+'\r\nContent-Disposition: form-data; name="'+name+'"'+('; filename="probe.elf"' if name == 'elf' else '')
        body += h.encode()+b'\r\n\r\n'+data+b'\r\n'
    body += ('--'+boundary+'--\r\n').encode()
    r = urllib.request.Request('http://'+settings['host']+':8080/elfldr', data=body,
                               headers={'Content-Type': 'multipart/form-data; boundary='+boundary})
    with urllib.request.urlopen(r, timeout=15) as q:
        return q.read(4096)

def fetch(remote, local):
    with deploy.connect(**settings, timeout=20) as ftp:
        data = deploy.remote_bytes(ftp, remote)
    pathlib.Path(local).write_bytes(data)
    return data

if __name__ == '__main__':
    if sys.argv[1] == 'ctl': print(ctl(' '.join(sys.argv[2:])).strip())
    elif sys.argv[1] == 'payload':
        reply = payload(sys.argv[2]); (OUT / 'last-loader-response.txt').write_bytes(reply)
        print('Payload submitted; response captured')
    elif sys.argv[1] == 'inventory':
        payload(ROOT/'build/network-elevation/inventory.elf'); time.sleep(1)
        data = fetch('/data/homebrew/PPSA99169/tests/privilege-inventory.txt', OUT/sys.argv[2])
        for line in data.decode().splitlines():
            if any(x in line.lower() for x in ('observer', 'eboot', 'lapy', 'daemon', 'proton', 'websrv', 'elfldr', 'ps5vk')): print(line)
