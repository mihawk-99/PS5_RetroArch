import sys,pathlib,struct,collections,json,re,hashlib
folder=pathlib.Path(sys.argv[1]);result={'mode':sys.argv[2],'wire':[],'raw_sha256':{}}
for path in sorted(folder.glob('wire-*.bin')):
 data=path.read_bytes();result['raw_sha256'][path.name]=hashlib.sha256(data).hexdigest()
 start=data.find(b'RANP',0,20)
 if start<0:continue
 offset=start+24;counts=collections.Counter();frames=[];buttons=set();nonzero=0
 while offset+8<=len(data):
  cmd,size=struct.unpack_from('!II',data,offset)
  assert size<=1<<20
  if offset+8+size>len(data):break
  payload=data[offset+8:offset+8+size];counts[f'{cmd:04x}']+=1
  if cmd==3:
   words=struct.unpack('!'+('I'*(size//4)),payload);frames.append(words[0]);buttons.update(words[2:]);nonzero+=any(words[2:])
  offset+=8+size
 result['wire'].append({'file':path.name,'commands':dict(counts),'first_frame':min(frames) if frames else None,'last_frame':max(frames) if frames else None,'input_packets':len(frames),'nonzero_input_packets':nonzero,'button_values':sorted(buttons),'incomplete_tail_bytes':len(data)-offset})
for name in ['retroarch.log','pc.log','trace.txt','startup-paths.log','tests_lapy-elevation.log']:
 data=(folder/name).read_bytes();result['raw_sha256'][name]=hashlib.sha256(data).hexdigest()
 log=data.decode(errors='replace')
 if name=='retroarch.log':
  checks=re.findall(r'State check frame (\d+): local=([0-9a-f]+) remote=([0-9a-f]+) match=(\d)',log)
  result['state_checks']=[{'frame':int(f),'local':a,'remote':b,'match':v=='1'} for f,a,b,v in checks]
  result['console_connected']='Connected to:' in log or 'has joined as player 2' in log
  result['netplay_errors']=[line for line in log.splitlines() if '[Netplay]' in line and ('[ERROR]' in line or 'mismatch' in line)]
 if name=='tests_lapy-elevation.log':result['daemon_balanced']='stage=complete error=0' in log and 'root_balanced=1' in log
 if name=='pc.log':result['pc_connected']='Connected to:' in log or 'has joined as player 2' in log
(folder/'summary.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps({k:v for k,v in result.items() if k not in ['raw_sha256','state_checks']},indent=2))
checks=result.get('state_checks',[]);print('State checks:',len(checks),'matching:',sum(x['match'] and x['local']==x['remote'] for x in checks))

assert result['console_connected'] and result['pc_connected'], 'Handshake incomplete'
assert result['daemon_balanced'], 'Daemon cleanup not verified'
assert not result['netplay_errors'], 'NetPlay reported an error'
assert len(result['wire']) == 2, 'Expected both peer streams'
assert all(row['input_packets'] >= 1200 for row in result['wire']), 'Insufficient sustained frame traffic'
assert any(row['nonzero_input_packets'] >= 180 for row in result['wire']), 'Scripted inputs missing'
assert any(row['commands'].get('0040', 0) >= 20 for row in result['wire']), 'State checks missing'
if result['mode'] == 'relay-join':
 assert len(checks) >= 20 and all(row['match'] and row['local']==row['remote'] for row in checks), 'Emulation states differ or were not checked'
print('PASS: console connection, frame traffic, scripted inputs and cleanup')
