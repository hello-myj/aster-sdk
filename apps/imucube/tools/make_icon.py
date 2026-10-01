"""Generate the launcher ASTI RGB565A8 cube icon without image dependencies."""
from pathlib import Path
import struct

SIZE=96
FACES=[([(48,19),(76,35),(48,51),(20,35)],(63,154,244)),
       ([(20,35),(48,51),(48,80),(20,64)],(106,82,205)),
       ([(48,51),(76,35),(76,64),(48,80)],(28,185,162))]

def inside(x,y,polygon):
    sign=None
    for i,(ax,ay) in enumerate(polygon):
        bx,by=polygon[(i+1)%len(polygon)]
        c=(bx-ax)*(y-ay)-(by-ay)*(x-ax)
        if c:
            current=c>0
            if sign is not None and sign!=current:return False
            sign=current
    return True

pixels=bytearray();alpha=bytearray()
for y in range(SIZE):
    for x in range(SIZE):
        cx=max(20,min(75,x));cy=max(20,min(75,y))
        coverage=255 if (x-cx)**2+(y-cy)**2<=400 else 0
        color=(17+y//8,29+y//6,49+y//5)
        for polygon,face in FACES:
            if inside(x+.5,y+.5,polygon):color=face
        r,g,b=color
        pixels+=struct.pack('<H',((r>>3)<<11)|((g>>2)<<5)|(b>>3))
        alpha.append(coverage)
blob=struct.pack('<4sHHHBBI',b'ASTI',1,SIZE,SIZE,4,0,SIZE*SIZE*3)+pixels+alpha
(Path(__file__).resolve().parents[1]/'icon.bin').write_bytes(blob)
