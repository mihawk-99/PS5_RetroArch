import numpy as np
from PIL import Image
from pathlib import Path
import sys
base = Path(sys.argv[1])
x,y=np.meshgrid(np.arange(3840,dtype=np.uint32),np.arange(2160,dtype=np.uint32))
sx=x&127;sy=y&127
local=((sy<<4)&0x70)^((sy<<5)&0xf00)^((sy<<9)&0x1000)^((sy<<8)&0x4000)^((sx<<2)&0xc)^((sx<<5)&0x380)^((sx<<4)&0x400)^((sx<<6)&0x800)^((sx<<9)&0xa000)
offset=((y//128)*30+x//128)*0x10000+local
for frame in (120,121):
 raw=np.fromfile(base / f'gpu-target-{frame}.bin',dtype=np.uint8)
 target=raw[offset[:,:,None]+np.arange(4)].copy()[:,:,[2,1,0,3]]
 Image.fromarray(target).save(base / f'target-{frame}.png')
 Image.fromarray(target[:,:,:3]).resize((1280,720)).save(base / f'target-{frame}-preview.png')
 print(frame,'target RGB minima/maxima',target[:,:,:3].min(axis=(0,1)),target[:,:,:3].max(axis=(0,1)))
