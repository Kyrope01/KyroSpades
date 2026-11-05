"""Check the rebuilt default optic, original reticle and transparent aperture."""
from pathlib import Path
import struct
import zlib

root=Path(__file__).resolve().parents[1]
png=(root/'resources.override/png/semi.png').read_bytes()
assert png[:8] == b'\x89PNG\r\n\x1a\n'
w,h,depth,color=struct.unpack_from('>IIBB',png,16)
assert (w,h,depth,color)==(1676,1260,8,6)
pos=8
compressed=bytearray()
while pos<len(png):
    n=struct.unpack_from('>I',png,pos)[0]
    kind=png[pos+4:pos+8]
    if kind==b'IDAT': compressed.extend(png[pos+8:pos+8+n])
    pos+=12+n
raw=zlib.decompress(compressed); stride=w*4; rows=[]; prev=bytearray(stride)
for y in range(h):
    start=y*(stride+1); kind=raw[start]
    row=bytearray(raw[start+1:start+1+stride])
    for i in range(stride):
        left=row[i-4] if i>=4 else 0
        up=prev[i]; corner=prev[i-4] if i>=4 else 0
        if kind==0: pred=0
        elif kind==1: pred=left
        elif kind==2: pred=up
        elif kind==3: pred=(left+up)//2
        elif kind==4:
            p=left+up-corner
            a,b,c=abs(p-left),abs(p-up),abs(p-corner)
            pred=left if a<=b and a<=c else (up if b<=c else corner)
        else: raise AssertionError('invalid PNG filter')
        row[i]=(row[i]+pred)&255
    rows.append(row); prev=row
alpha=lambda x,y: rows[y][x*4+3]
rgba=lambda x,y: tuple(rows[y][x*4:x*4+4])
assert all(row[3] == 0 and row[-1] == 0 for row in rows)
assert alpha(700,400)==0 and alpha(600,400)==0  # open field
assert alpha(400,500)==255  # opaque housing
# The housing is flat dark grey, with no inset light-grey lines.
for x in (332, 380, 388):
    assert rgba(x, 500)==(27,26,28,255)
r,g,b,a=rgba(838,634)
assert r>200 and g<50 and b<50 and a>200  # original red-dot location
assert alpha(838,500)>0 and alpha(838,1000)>0  # vertical reticle
# Reinforced upper and lower shoulder sections on both sides.
for x,y in ((380,370), (1296,370), (380,850), (1296,850)):
    assert rgba(x,y)==(27,26,28,255)
# Retain a long vertical side and discrete stepped corners, not a smooth circle.
for y in (480, 560, 640, 720):
    assert alpha(300,y)==0 and alpha(320,y)==255
assert alpha(400,300)==0 and alpha(400,500)==255
print('Default optic stepped voxel rim, aperture and original reticle checks passed')
