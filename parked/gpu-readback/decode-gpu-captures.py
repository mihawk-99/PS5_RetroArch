import numpy as np
from PIL import Image
from pathlib import Path
x,y=np.meshgrid(np.arange(3840,dtype=np.uint32),np.arange(2160,dtype=np.uint32))
sx=x&127;sy=y&127
local=((sy<<4)&0x70)^((sy<<5)&0xf00)^((sy<<9)&0x1000)^((sy<<8)&0x4000)^((sx<<2)&0xc)^((sx<<5)&0x380)^((sx<<4)&0x400)^((sx<<6)&0x800)^((sx<<9)&0xa000)
offset=((y//128)*30+x//128)*0x10000+local
for frame in (8,9):
 menu=np.fromfile(f'klog/gpu-menu-{frame}.bin',dtype=np.uint8).reshape(240,320,4)
 Image.fromarray(menu).save(f'klog/gpu-menu-{frame}.png')
 raw=np.fromfile(f'klog/gpu-target-{frame}.bin',dtype=np.uint8)
 target=raw[offset[:,:,None]+np.arange(4)].copy()
 # WSI B8G8R8A8 image, VIDEOOUT SWAP_ALT, bytes in BGRA order.
 target=target[:,:,[2,1,0,3]]
 Image.fromarray(target).save(f'klog/gpu-target-{frame}.png')
 Image.fromarray(target[:,:,:3]).resize((960,540)).save(f'klog/gpu-target-{frame}-preview.png')
 colors,counts=np.unique(menu.reshape(-1,4),axis=0,return_counts=True)
 print(frame,'menu colors',len(colors),'dominant',[(colors[i].tolist(),int(counts[i])) for i in np.argsort(counts)[-6:]])
 print(frame,'target RGB minima/maxima',target[:,:,:3].min(axis=(0,1)),target[:,:,:3].max(axis=(0,1)))
