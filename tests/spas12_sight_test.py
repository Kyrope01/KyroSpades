"""Verify the SPAS-12 ADS PNG alignment and transparent widescreen edges."""
from pathlib import Path
import struct
import zlib

root=Path(__file__).resolve().parents[1]
png=(root/'resources.override/png/shotgun(SPAS-12).png').read_bytes()
assert png[:8] == b'\x89PNG\r\n\x1a\n'
w,h,depth,color=struct.unpack_from('>IIBB',png,16)
assert (w,h,depth,color)==(1024,768,8,6)
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
assert all(row[3]==0 and row[-1]==0 for row in rows)  # HUD side stretch
assert alpha(512,383)==0 and alpha(512,384)==255  # point of aim
assert all(alpha(512,y)==255 for y in range(384,395))
assert alpha(511,393)==0 and alpha(513,393)==0  # one-pixel post
assert alpha(511,394)==255 and alpha(513,394)==255  # post joins raised base
assert alpha(487,380)==255 and alpha(537,380)==255  # hood sides
assert alpha(512,376)==0  # transparent sight picture
assert alpha(512,346)==0 and alpha(512,357)==255  # lowered, shorter hood
assert 'shotgun(SPAS-12).png' in (root/'src/CMakeLists.txt').read_text()
print('SPAS-12 ADS alpha, one-pixel aim, and packaging checks passed')
