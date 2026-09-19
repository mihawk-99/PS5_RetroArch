from pathlib import Path
import re,struct,numpy as np
trace=Path('klog/gpu-resources-latest-trace.txt').read_text()
for kind,index,addr,size in re.findall(r'gpu resources: (\w+) (\d+) address=(\w+) bytes=(\d+)',trace):
 b=Path(f'klog/gpu-{kind}-{index}.bin').read_bytes();base=int(addr,16)
 if kind=='stages':
  print('STAGE',index)
  for off in (0,0x2000):
   print(' header',hex(off),'stage',b[off+90],'inputs',struct.unpack_from('<I',b,off+80)[0],'outputs',struct.unpack_from('<H',b,off+86)[0])
   for k,label,count in [(24,'cx',b[off+91]),(48,'inputs',struct.unpack_from('<I',b,off+80)[0]),(56,'outputs',struct.unpack_from('<H',b,off+86)[0])]:
    at=struct.unpack_from('<Q',b,off+k)[0]-base
    if count and 0<=at<len(b):
     print(label,[(hex(a),hex(v)) for a,v in struct.iter_unpack('<II',b[at:at+count*8]) ] if k==24 else [hex(x) for x in struct.unpack_from('<'+'I'*count,b,at)])
  print('link',[(hex(a),hex(v)) for a,v in struct.iter_unpack('<II',b[0x5000:0x5000+34*8])][:4])
 if kind=='buffers' and int(size)==65536:
  floats=np.frombuffer(b,dtype='<f4');print('BUFFER',index,hex(base),'first 64 floats',floats[:64].tolist())
