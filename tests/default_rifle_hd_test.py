"""Verify the high-resolution default rifle and preserved bundled sounds."""
from pathlib import Path
import hashlib
import struct
from zipfile import ZipFile
root = Path(__file__).resolve().parents[1]
data = (root/'resources.override/kv6/semi.kv6').read_bytes()
magic,xs,ys,zs,xp,yp,zp,n=struct.unpack_from('<4sIIIfffI',data)
assert (magic,xs,ys,zs)==(b'Kvxl',20,176,52)
assert 6000 < n < 15000
scale=.05*44/ys
assert abs(ys*scale - 44*.05)<1e-6
assert abs(yp*scale - 16.5*.05)<1e-6
assert abs((zs-zp)*scale - (13-8.5)*.05)<1e-6
ix=32+n*8
xc=struct.unpack_from(f'<{xs}I',data,ix); ix+=xs*4
counts=struct.unpack_from(f'<{xs*ys}H',data,ix)
assert sum(xc)==sum(counts)==n and len(data)==ix+xs*ys*2
assert all(sum(counts[x*ys:(x+1)*ys])==xc[x] for x in range(xs))
offset=0
points={}
for x in range(xs):
    for y in range(ys):
        prev=-1
        for _ in range(counts[x*ys+y]):
            b,g,r,a,z,mask,normal=struct.unpack_from('<BBBBHBB',data,32+offset*8)
            assert prev < z < zs and mask and (r,g,b)!=(0,0,0)
            points[x,y,z]=(r,g,b)
            prev=z; offset+=1
# Scope is open through its optical axis, not capped at either end.
assert all((9,y,10) not in points and (10,y,10) not in points for y in range(44,85))
assert (9,44,6) in points and (9,84,6) in points  # both rims exist
assert (9,65,8) in points and (11,65,10) in points  # recessed details

png=(root/'resources.override/png/semi.png').read_bytes()
assert png[:8]==b'\x89PNG\r\n\x1a\n'
assert struct.unpack_from('>IIBB',png,16)==(1676,1260,8,6)
# Default sound and Classic assets continue to come from the original archive.
assert not (root/'resources.override/wav/semishoot.wav').exists()
assert not (root/'resources.override/kv6/semi(Classic).kv6').exists()
assert not (root/'resources.override/png/semi(Classic).png').exists()
with ZipFile(root/'bsresources.zip') as z:
    for name in ('wav/semishoot.wav','wav/semishoot(Classic).wav','kv6/semi(Classic).kv6','png/semi(Classic).png'):
        assert z.read(name)
assert hashlib.sha256((root/'resources.override/wav/semishoot(Kar-98K).wav').read_bytes()).hexdigest()=='e5a7f646de7772e0df359284d546391ea24bf42cad7762c6010ebd474995db1f'
cmake=(root/'src/CMakeLists.txt').read_text()
assert 'resources.override/kv6/semi.kv6' in cmake and 'resources.override/png/semi.png' in cmake
assert 'weapon_default_rifle_scale' in (root/'src/model.c').read_text()
assert 'weapon_default_rifle_scale' in (root/'src/skins.c').read_text()
print('Default HD rifle structure, sizing, scope format and unchanged sounds passed')
