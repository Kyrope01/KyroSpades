"""Structural and skin integration checks for the SCAR-H KV6 model."""
from pathlib import Path
import struct

root = Path(__file__).resolve().parents[1]
path = root/'resources.override/kv6/smg(SCAR-H).kv6'
data = path.read_bytes()
magic,xs,ys,zs,xp,yp,zp,n = struct.unpack_from('<4sIIIfffI',data)
assert (magic,xs,ys,zs) == (b'Kvxl',23,144,55)
assert 5000 < n < 12000
# Compare the scaled pivot with the default SMG's original pivot.
scale = .05 * 36 / ys
assert abs(yp*scale - 17.5*.05) < .0001
assert abs((zs-zp)*scale - (14-7.5)*.05) < .003
assert abs(xp*scale - 2*.05) < .001
index = 32+n*8
xcounts = struct.unpack_from(f'<{xs}I',data,index)
index += xs*4
counts = struct.unpack_from(f'<{xs*ys}H',data,index)
assert len(data) == index+xs*ys*2
assert sum(xcounts)==sum(counts)==n
assert all(sum(counts[x*ys:(x+1)*ys])==xcounts[x] for x in range(xs))
offset=0
points={}
for x in range(xs):
    for y in range(ys):
        last=-1
        for _ in range(counts[x*ys+y]):
            b,g,r,_,z,mask,normal=struct.unpack_from('<BBBBHBB',data,32+offset*8)
            assert last < z < zs and 0 < mask < 64 and (r,g,b)!=(0,0,0)
            points[x,y,z]=mask
            last=z
            offset+=1
for (x,y,z),mask in points.items():
    for dx,dy,dz,bit in [(-1,0,0,1),(1,0,0,2),(0,-1,0,4),(0,1,0,8),(0,0,-1,16),(0,0,1,32)]:
        if (x+dx,y+dy,z+dz) in points:
            assert not (mask&bit)
assert min(y for x,y,z in points)==0 and max(y for x,y,z in points)==143
skins=(root/'src/skins.c').read_text()
assert '"smg", 0.05F' in skins and 'case SKIN_SMG:' in skins
assert 'skin_reload_model((enum skin_category_type)c, cat, m, &e->previews[m], mp)' in skins
assert 'skin_reload_model(category, cat, m, cat->models[m].ptr, path)' in skins
assert 'weapon_skin_scale(target->scale, target->ysiz)' in skins
assert 'smg(SCAR-H).kv6' in (root/'src/CMakeLists.txt').read_text()
print('SCAR-H KV6 structure, pivot and skin integration checks passed')
