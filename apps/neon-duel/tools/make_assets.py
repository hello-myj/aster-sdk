"""Original code-authored pixel artwork and synthesized audio; no external assets.
Rebuild with Python + Pillow. Outputs a shared C sprite atlas and PNG originals.
"""
from pathlib import Path
from PIL import Image, ImageDraw
import math, random, struct, wave

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'assets'
OUT.mkdir(exist_ok=True)
PALETTE = ['#000000','#101629','#23304b','#435877','#d4f5ff',
           '#ffce9e','#bf795b','#603d43','#22d9e9','#087f9f',
           '#a044ed','#ffd35c','#ef497d','#922c62','#eef6e7','#17283d']
RGB = [tuple(bytes.fromhex(c[1:])) for c in PALETTE]
def rgb565(c):
    r,g,b=c
    return ((r>>3)<<11)|((g>>2)<<5)|(b>>3)
sprites=[]
sheet=Image.new('RGB',(52*9,72*2))
for fighter in range(2):
    for pose in range(9):
        im=Image.new('P',(52,72)); im.putpalette([v for c in RGB for v in c]+[0]*(768-48))
        d=ImageDraw.Draw(im)
        def poly(pts,c): d.polygon(pts,fill=c)
        def limb(a,b,w,c):
            d.line([a,b],fill=1,width=w+2); d.line([a,b],fill=c,width=w)
        suit=8 if fighter==0 else 12; dark=9 if fighter==0 else 13
        bob=1 if pose==1 else 0
        hip=(24,43+bob); shoulder=(25,23+bob)
        feet=[(17,65),(33,65)]; knees=[(18,53),(30,54)]
        if pose==1: feet=[(9,65),(39,65)]; knees=[(15,53),(33,52)]
        if pose==3: knees=[(18,55),(36,42)]; feet=[(14,65),(49,40)]
        if pose==5: knees=[(16,46),(34,46)]; feet=[(10,54),(39,54)]
        if pose==6: shoulder=(20,25); hip=(22,44); feet=[(12,65),(29,65)]
        if pose==7:
            poly([(6,56),(17,53),(38,57),(46,64),(43,68),(15,66),(5,64)],dark)
            d.rectangle((37,50,46,57),fill=5); d.rectangle((39,48,48,51),fill=1)
        else:
            for k,f in zip(knees,feet):
                limb(hip,k,8,dark); limb(k,f,7,suit)
                d.rectangle((f[0]-4,f[1]-1,f[0]+6,f[1]+3),fill=1)
                d.rectangle((f[0]-3,f[1]-1,f[0]+5,f[1]+1),fill=3)
            poly([(shoulder[0]-8,shoulder[1]-1),(shoulder[0]+7,shoulder[1]),(hip[0]+7,hip[1]),(hip[0]-7,hip[1])],1)
            poly([(shoulder[0]-6,shoulder[1]),(shoulder[0]+5,shoulder[1]),(hip[0]+5,hip[1]-2),(hip[0]-5,hip[1]-2)],suit)
            poly([(shoulder[0]-6,shoulder[1]),(shoulder[0]-2,shoulder[1]+1),(hip[0]-1,hip[1]-3),(hip[0]-5,hip[1]-3)],dark)
            d.rectangle((hip[0]-7,hip[1]-3,hip[0]+6,hip[1]),fill=11)
            head=(shoulder[0],shoulder[1]-11)
            d.rectangle((head[0]-5,head[1]-6,head[0]+5,head[1]+7),fill=1)
            d.rectangle((head[0]-4,head[1]-4,head[0]+5,head[1]+5),fill=5)
            d.rectangle((head[0]-4,head[1]-4,head[0]-1,head[1]+5),fill=6)
            poly([(head[0]-6,head[1]-3),(head[0]-4,head[1]-8),(head[0]+3,head[1]-8),(head[0]+7,head[1]-4),(head[0]+2,head[1]-3)],1)
            d.rectangle((head[0]+2,head[1]-1,head[0]+5,head[1]),fill=1)
            d.rectangle((head[0]-5,head[1]-3,head[0]+5,head[1]-2),fill=suit)
            d.line([(head[0]-5,head[1]-2),(head[0]-10,head[1]+2),(head[0]-14,head[1]+1)],fill=suit,width=2)
            back_elbow=(16,34); back_hand=(22,29); elbow=(34,30); hand=(37,22)
            if pose==2: elbow=(36,25); hand=(49,23)
            if pose==3: elbow=(30,29); hand=(34,22)
            if pose==4: elbow=(32,23); hand=(29,12); back_hand=(33,17)
            if pose==5: elbow=(33,29); hand=(39,23)
            if pose==6: elbow=(13,33); hand=(9,25)
            if pose==8: elbow=(36,32); hand=(46,28); back_hand=(39,33)
            limb((shoulder[0]-4,shoulder[1]+3),back_elbow,5,dark); limb(back_elbow,back_hand,4,6)
            limb((shoulder[0]+4,shoulder[1]+3),elbow,6,suit); limb(elbow,hand,5,5)
            d.rectangle((hand[0]-3,hand[1]-3,hand[0]+3,hand[1]+2),fill=14)
            d.rectangle((hand[0]-3,hand[1],hand[0]+3,hand[1]+2),fill=3)
        sprites.append(list(im.getdata()))
        sheet.paste(im.convert('RGB'),(pose*52,fighter*72))
sheet.save(OUT/'fighters.png')
# An original 320x240 scene, authored as deterministic pixel shapes.
bg=Image.new('RGB',(320,240)); d=ImageDraw.Draw(bg)
for y in range(240):
    t=min(y,180)/180; d.line((0,y,320,y),fill=(int(13+18*t),int(16+9*t),int(34+24*t)))
random.seed(77)
for _ in range(75):
    x,y=random.randrange(320),random.randrange(8,105); d.point((x,y),fill='#647c9d')
d.ellipse((218,29,265,76),fill='#9fb8c6');d.ellipse((211,22,254,70),fill='#1b2030')
for layer in range(2):
    x=-5
    while x<320:
        w=random.randrange(12,32); top=random.randrange(65,125)+layer*19
        col='#172039' if layer==0 else '#222840'
        d.rectangle((x,top,x+w,166),fill=col)
        for wy in range(top+6,159,8):
            for wx in range(x+3,x+w-2,6):
                if random.random()<.52: d.rectangle((wx,wy,wx+1,wy+2),fill=random.choice(['#395e78','#695079','#288594']))
        x+=w+3
d.rectangle((23,99,53,121),fill='#101629');d.rectangle((25,101,51,119),outline='#ef497d',width=2)
d.line((30,108,46,108),fill='#ffc1da',width=2);d.line((33,113,43,113),fill='#ffc1da',width=2)
d.rectangle((273,87,281,133),fill='#101629');d.line((277,92,277,128),fill='#22d9e9',width=2)
d.rectangle((0,155,319,158),fill='#435877')
for x in range(0,320,22): d.rectangle((x,154,x+2,180),fill='#3a455b')
d.rectangle((0,180,319,239),fill='#1a2135'); d.line((0,181,319,181),fill='#a06698',width=2)
for x in range(-160,480,55): d.line((160,165,x,239),fill='#33364c')
for y in [190,208,232]: d.line((0,y,319,y),fill='#33364c')
d.line((15,184,89,184),fill='#168193');d.line((229,184,304,184),fill='#983559')
bg.save(OUT/'arena.png')
# Compile RGB565 background + RLE indexed sprite frames; no file decode per frame.
with (ROOT/'src/art.h').open('w') as f:
    f.write('/* Generated by tools/make_assets.py: original artwork. */\n#include <stdint.h>\n')
    f.write('static const uint16_t art_palette[16]={'+','.join(hex(rgb565(c)) for c in RGB)+'};\n')
    f.write('static const uint16_t art_arena[320*240]={\n')
    vals=[rgb565(c) for c in bg.getdata()]
    for i in range(0,len(vals),80):f.write(','.join(hex(v) for v in vals[i:i+80])+',\n')
    f.write('};\n')
    offsets=[]; runs=[]
    for data in sprites:
        offsets.append(len(runs));i=0
        while i<len(data):
            c=data[i];n=1
            while i+n<len(data) and data[i+n]==c and n<255:n+=1
            runs.extend([n,c]);i+=n
    offsets.append(len(runs))
    f.write('static const uint32_t art_offsets[]={'+','.join(map(str,offsets))+'};\n')
    f.write('static const uint8_t art_runs[]={'+','.join(map(str,runs))+'};\n')
# Stereo 44.1kHz signed PCM WAVs, short enough for the four-slot effects cache.
for name,duration in [('hit',.10),('swing',.09),('special',.18),('bell',.15)]:
    frames=[]
    for i in range(int(duration*44100)):
        t=i/44100;e=(1-t/duration)**2
        noise=random.uniform(-1,1)
        if name=='hit':s=(noise*.65+math.sin(2*math.pi*110*t)*.35)*e
        elif name=='swing':s=noise*.32*math.sin(math.pi*t/duration)*e
        elif name=='special':s=(math.sin(2*math.pi*(180*t+1400*t*t))*.7+noise*.25)*e
        else:s=(math.sin(2*math.pi*880*t)+math.sin(2*math.pi*1320*t)*.4)*e*.5
        v=int(max(-1,min(1,s)) * 13000);frames.append(struct.pack('<hh',v,v))
    with wave.open(str(OUT/(name+'.wav')),'wb') as w:
        w.setparams((2,2,44100,0,'NONE','not compressed'));w.writeframes(b''.join(frames))
    # lckfb-s3 reports a 16kHz stereo output; preserve the simulator's 44.1kHz set.
    native=OUT/'16000';native.mkdir(exist_ok=True)
    target=[]
    for j in range(int(duration*16000)):
        source=j*44100/16000;index=int(source);fraction=source-index
        left=struct.unpack('<hh',frames[index])[0]
        right=struct.unpack('<hh',frames[min(index+1,len(frames)-1)])[0]
        sample=round(left+(right-left)*fraction)
        target.append(struct.pack('<hh',sample,sample))
    with wave.open(str(native/(name+'.wav')),'wb') as w:
        w.setparams((2,2,16000,0,'NONE','not compressed'));w.writeframes(b''.join(target))
ico=sheet.crop((0,0,52,72)).resize((35,48),Image.Resampling.NEAREST)
canvas=Image.new('RGB',(48,48),'#101629');canvas.paste(ico,(6,0))
payload=b''.join(struct.pack('<H',rgb565(c)) for c in canvas.getdata())
(ROOT/'icon.bin').write_bytes(b'ASTI'+struct.pack('<HHHBBI',1,48,48,3,0,len(payload))+payload)
print('Generated arena, 18 fighter frames, four effects, C atlas and ASTI icon.')
