"""Checks source-derived voxel skins, sound matching and loading integration."""
from pathlib import Path
from zipfile import ZipFile
import struct, wave, io, zlib

root=Path(__file__).resolve().parents[1]
packs=[
 ('AK-12.zip','smg','AK-12 Black'),('Black - Kobra RDS.zip','smg','AK-12 Kobra'),
 ('Black - PSO-1.zip','smg','AK-12 PSO-1'),('Nickel and Black - Ironsights.zip','smg','AK-12 Nickel'),
 ('Uzi (Ember).zip','smg','Uzi Ember'),
 ('BluePrint USP-S (Ember).zip','semi','USP-S BluePrint'),('RoyalBlue USP-S (Ember).zip','semi','USP-S RoyalBlue'),
 ('DE Asiimov.zip','semi','Desert Eagle Asiimov'),('DE Blaze.zip','semi','Desert Eagle Blaze'),
 ('M9 (Diamond).zip','semi','M9 Diamond'),('Baseball Grenade (Penguano).zip','grenade','Baseball Penguano')]
def png_rows(path):
 data=path.read_bytes(); assert data[:8]==b'\x89PNG\r\n\x1a\n'
 w,h,depth,color=struct.unpack_from('>IIBB',data,16);assert (w,h,depth,color)==(1024,768,8,6)
 i=8;compressed=bytearray()
 while i<len(data):
  n=struct.unpack_from('>I',data,i)[0]; kind=data[i+4:i+8]
  if kind==b'IDAT':compressed.extend(data[i+8:i+8+n])
  i+=n+12
 raw=zlib.decompress(compressed);stride=w*4;prev=bytearray(stride);rows=[]
 for y in range(h):
  kind=raw[y*(stride+1)];row=bytearray(raw[y*(stride+1)+1:(y+1)*(stride+1)])
  for j in range(stride):
   left=row[j-4] if j>=4 else 0;up=prev[j];corner=prev[j-4] if j>=4 else 0
   if kind==0: pred=0
   elif kind==1:pred=left
   elif kind==2:pred=up
   elif kind==3:pred=(left+up)//2
   elif kind==4:
    p=left+up-corner;a,b,c=abs(p-left),abs(p-up),abs(p-corner)
    pred=left if a<=b and a<=c else up if b<=c else corner
   else:raise AssertionError('invalid png filter')
   row[j]=(row[j]+pred)&255
  rows.append(row);prev=row
 return rows

cmake=(root/'src/CMakeLists.txt').read_text(); registry=(root/'src/legacy_mod_skin.h').read_text()
for archive,category,name in packs:
 with ZipFile(root/'tools/legacy_mods/source'/archive) as z:
  members=z.namelist(); kv=next(n for n in members if n.endswith('/kv6/'+category+'.kv6'))
  original=z.read(kv); asset=f'{category}({name})'
  data=(root/'resources.override/kv6'/f'{asset}.kv6').read_bytes()
  assert data == original, f'{name}: model changed from source'
  assert f'resources.override/kv6/{asset}.kv6' in cmake
  if category!='grenade':
   png=root/'resources.override/png'/f'{asset}.png'
   original_sight=[n for n in members if n.lower().endswith('/png/'+category+'.png')]
   if original_sight and not (name.startswith('Desert Eagle ') or name == 'M9 Diamond'):
    assert png.read_bytes()==z.read(original_sight[0]), f'{name}: supplied sight changed'
   else:
    rows=png_rows(png)
    assert all(row[3]==0 and row[-1]==0 for row in rows), name
    assert rows[384][512*4+3]>0, name
   assert f'resources.override/png/{asset}.png' in cmake
  for sound in (('semishoot','semireload') if category=='semi' else ('smgshoot','smgreload') if category=='smg' else ()):
   source=[n for n in members if n.endswith('/wav/'+sound+'.wav')]
   target=root/'resources.override/wav'/f'{sound}({name}).wav'
   if source:
    assert target.read_bytes()==z.read(source[0]),name
    with wave.open(io.BytesIO(target.read_bytes())) as f:
     assert f.getnchannels() in (1,2) and f.getsampwidth()==2 and f.getnframes()>0
    assert f'resources.override/wav/{target.name}' in cmake
   else:assert not target.exists(), name
print('11 original models, replacement sights, source sounds and packaging passed')

# Test the actual path classifier compiled as C, plus reload integration and
# the scale-sensitive tool switcher's pivot calculations.
import subprocess, tempfile
with tempfile.TemporaryDirectory() as d:
 exe=Path(d)/'registry'
 subprocess.run(['cc','-std=c99','-Wall','-Wextra','-Werror',str(root/'tests/legacy_mod_registry_test.c'),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
skins=(root/'src/skins.c').read_text()
assert 'legacy_mod_model_path(path)' not in skins and 'target->scale *= 0.25F' not in skins
assert 'legacy_mod_reload_suffix(suffix)' in skins and 'sound_uses_skin_suffix(category, s, e->suffix)' in skins and '&sound_rifle_reload, "semireload"' in skins
hud=(root/'src/hud.c').read_text()
assert '(gun->xpiv - gun->xsiz / 2) * gun->scale' in hud
assert '(model_grenade.xpiv - model_grenade.xsiz / 2) * model_grenade.scale' in hud
print('Original skin scale, fallback reload and HUD pivot wiring passed')

# The two AK iron variants share a single centered post and broad rear
# shoulders. Kobra has a closed hood, transparent lens, and only a red dot.
for name in ('AK-12 Black', 'AK-12 Nickel'):
 rows=png_rows(root/'resources.override/png'/f'smg({name}).png')
 def pixel(x,y): return tuple(rows[y][x*4:x*4+4])
 assert pixel(512,384)[3] == 255
 assert pixel(500,384)[3] == 0 and pixel(524,384)[3] == 0
 assert pixel(472,385)[3] == 255 and pixel(552,385)[3] == 255
rows=png_rows(root/'resources.override/png/smg(AK-12 Kobra).png')
def pixel(x,y): return tuple(rows[y][x*4:x*4+4])
assert pixel(512,384)==(235,43,30,255)
assert pixel(512,340)[3] == 255 and pixel(466,384)[3] == 255 and pixel(558,384)[3] == 255
assert pixel(500,384)[3] == 0 and pixel(524,384)[3] == 0
print('AK notch and Kobra closed hood/dot checks passed')

# Removed skins must not be rediscovered or included in build dependencies.
assert not (root/'resources.override/kv6/smg(HK416 Aeon).kv6').exists()
assert not (root/'resources.override/png/smg(HK416 Aeon).png').exists()
assert 'smg(HK416 Aeon)' not in cmake
print('HK416 Aeon removal checks passed')

# The Uzi receiver and lower stock should not have transparent seams.
rows=png_rows(root/'resources.override/png/smg(Uzi Ember).png')
for y in range(649,661):
 for x in range(422,603):
  assert rows[y][x*4+3] == 255
print('Uzi receiver-to-stock seam check passed')
