#!/usr/bin/env python3
"""Install original user-provided assets and create missing voxel sights.

The 4x grid below is only used to render previews and sights: the KV6 files
installed in game are copied directly from their archives. Requires Pillow.
"""
from pathlib import Path
from zipfile import ZipFile
import struct
from PIL import Image, ImageDraw, ImageFont

ROOT=Path(__file__).resolve().parents[2]
SOURCES=ROOT/'tools/legacy_mods/source'
OUT=ROOT/'resources.override'
PREVIEWS=ROOT/'docs/previews/legacy-mods'
PREVIEWS.mkdir(parents=True,exist_ok=True)
S=4
# Display suffixes intentionally contain no parentheses: skins_scan uses ')'
# as the suffix delimiter. AK-12.zip contains the black iron-sight variant.
PACKS=[
 ('AK-12.zip','smg','AK-12 Black','akiron'),
 ('Black - Kobra RDS.zip','smg','AK-12 Kobra','kobra'),
 ('Black - PSO-1.zip','smg','AK-12 PSO-1','pso'),
 ('Nickel and Black - Ironsights.zip','smg','AK-12 Nickel','akiron'),
 ('Uzi (Ember).zip','smg','Uzi Ember','uzi'),
 ('BluePrint USP-S (Ember).zip','semi','USP-S BluePrint','pistol'),
 ('RoyalBlue USP-S (Ember).zip','semi','USP-S RoyalBlue','pistol'),
 ('DE Asiimov.zip','semi','Desert Eagle Asiimov','eagle'),
 ('DE Blaze.zip','semi','Desert Eagle Blaze','eagle'),
 ('M9 (Diamond).zip','semi','M9 Diamond','m9'),
 ('Baseball Grenade (Penguano).zip','grenade','Baseball Penguano',None),
]
DIRS=[(-1,0,0,1),(1,0,0,2),(0,-1,0,4),(0,1,0,8),(0,0,-1,16),(0,0,1,32)]
try:
 font=ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf',24)
 small=ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf',16)
except OSError: font=small=ImageFont.load_default()

def load_kv6(data):
 magic,xs,ys,zs,xp,yp,zp,n=struct.unpack_from('<4sIIIfffI',data)
 assert magic==b'Kvxl' and n<10000
 off=32+8*n+4*xs
 counts=struct.unpack_from(f'<{xs*ys}H',data,off)
 idx=0; vox={}
 for x in range(xs):
  for y in range(ys):
   col=[]
   for _ in range(counts[x*ys+y]):
    b,g,r,_,z,mask,norm=struct.unpack_from('<BBBBHBB',data,32+8*idx);idx+=1
    col.append((z,(r,g,b),mask,norm))
   start=None
   for z,c,m,normal in col:
    if m&16: start=z
    if m&32:
     assert start is not None
     for zz in range(start,z+1):
      nearest=min(col,key=lambda e:abs(e[0]-zz))
      vox[x,y,zz]=(nearest[1],nearest[3])
     start=None
   assert start is None
 return (xs,ys,zs,xp,yp,zp),vox

def render_voxels(old):
 # Subdivision is for rendering only; exactly the same occupied cubes and
 # colours as the original KV6, without bevels or added geometry.
 return {(x*S+dx,y*S+dy,z*S+dz):(col,norm)
         for (x,y,z),(col,norm) in old.items()
         for dx in range(S) for dy in range(S) for dz in range(S)}

def surface(vox):
 result=[]
 for (x,y,z),(color,norm) in vox.items():
  mask=sum(bit for dx,dy,dz,bit in DIRS if (x+dx,y+dy,z+dz) not in vox)
  if mask:result.append((x,y,z,color,mask,norm))
 return result

def preview_model(name,dims,vox):
 xs,ys,zs,*_=dims; im=Image.new('RGB',(1440,620),(27,31,33));d=ImageDraw.Draw(im)
 d.text((38,25),name+' / ORIGINAL MODEL',font=font,fill=(232,230,218))
 d.text((38,63),'Orthographic projection of the original KV6',font=small,fill=(160,170,166))
 scale=min(7,1300//(ys*S),440//(zs*S)); scale=max(2,scale)
 ox=(1440-ys*S*scale)//2;oy=120+(440-zs*S*scale)//2
 for y in range(ys*S):
  for z in range(zs*S):
   candidates=[(x,vox[x,y,z][0]) for x in range(xs*S) if (x,y,z) in vox]
   if candidates:
    _,c=max(candidates); d.rectangle((ox+y*scale,oy+z*scale,ox+(y+1)*scale-1,oy+(z+1)*scale-1),fill=c)
 im.save(PREVIEWS/(name.replace(' ','-')+'-model.png'))

def sight(name,kind,dims,surf):
 xs,ys,zs,*_=dims; xs*=S;ys*=S;zs*=S
 # Identify the top of the muzzle, leaving optic-specific work to overlay.
 front=[z for x,y,z,c,m,n in surf if y>=ys-8]
 target_z=min(front) if front else zs//3
 cam_x=xs/2; cam_y=-ys*.52
 target_y=ys-3; focal=min(1900.,ys*28.)
 cam_z=target_z-4
 for x,y,z,c,m,n in surf:
  if y>ys*.80: continue
  dep=y-cam_y; t=dep/(target_y-cam_y)
  if t<.99:
   cam_z=min(cam_z,(z-34*dep/focal-target_z*t)/(1-t))
 # Uzi ADS should look down a level receiver, not down onto the top.
 if kind=='uzi':
  cam_y=-ys*1.15
  cam_z=target_z-24
 img=Image.new('RGBA',(1024,768),(0,0,0,0));d=ImageDraw.Draw(img)
 def project(p):
  x,y,z=p;dep=y-cam_y
  axis=cam_z+(target_z-cam_z)*dep/(target_y-cam_y)
  return round(512-(x-cam_x)*focal/dep),round(384+(z-axis)*focal/dep-(40 if kind in ('akiron','kobra','pso') else 0))
 faces=[(-1,0,0,1,[(0,0,0),(0,1,0),(0,1,1),(0,0,1)],.8),(1,0,0,2,[(1,0,0),(1,0,1),(1,1,1),(1,1,0)],.85),
        (0,-1,0,4,[(0,0,0),(0,0,1),(1,0,1),(1,0,0)],.75),(0,1,0,8,[(0,1,0),(1,1,0),(1,1,1),(0,1,1)],.75),
        (0,0,-1,16,[(0,0,0),(1,0,0),(1,1,0),(0,1,0)],1.18),(0,0,1,32,[(0,0,1),(0,1,1),(1,1,1),(1,0,1)],.65)]
 polys=[]
 for x,y,z,c,mask,n in surf:
  for nx,ny,nz,bit,corners,brightness in faces:
   if not mask&bit:continue
   cx=x+(1 if nx>0 else 0 if nx<0 else .5);cy=y+(1 if ny>0 else 0 if ny<0 else .5);cz=z+(1 if nz>0 else 0 if nz<0 else .5)
   if nx*(cam_x-cx)+ny*(cam_y-cy)+nz*(cam_z-cz)<=0:continue
   pts=[project((x+dx,y+dy,z+dz)) for dx,dy,dz in corners]
   col=tuple(min(255,int(v*brightness)) for v in c)+(255,)
   polys.append((cy,pts,col))
 for _,pts,col in sorted(polys,key=lambda p:p[0],reverse=True):d.polygon(pts,fill=col)
 if kind=='uzi':
  # Close the projection seam between the rear receiver and lower stock.
  d.rectangle((422,649,602,660),fill=(25,25,25,255))
 # Geometry behind the eye can otherwise protrude above the sight opening.
 # Clear that projected background before drawing the matching optic detail.
 d.rectangle((0,0,1023,354),fill=(0,0,0,0))
 # Make a readable unobstructed aim point. Sight details are extra PNG pixels,
 # while the rest of the visible gun is the actual projected KV6 model.
 if kind=='pso':
  d.rectangle((464,331,560,422),fill=(0,0,0,0))
  d.polygon([(464,344),(471,344),(471,335),(480,335),(480,331),(544,331),(544,335),(553,335),(553,344),(560,344),(560,418),(553,418),(553,422),(471,422),(471,418),(464,418)],fill=(28,30,29,255))
  d.ellipse((476,343,548,415),fill=(0,0,0,0))
  d.line((512,384,512,403),fill=(31,33,32,255),width=1)
  d.line((499,397,512,384,525,397),fill=(31,33,32,255),width=1)
  d.rectangle((464,418,560,424),fill=(28,30,29,255))
 elif kind=='kobra':
  # Broad stepped silhouette and rectangular voxel corners rather than a
  # smooth circle, while retaining the reference's enclosed reflex window.
  d.rectangle((465,337,559,426),fill=(0,0,0,0))
  d.polygon([(488,340),(536,340),(536,347),(547,347),(547,355),
             (554,355),(554,366),(558,366),(558,402),(554,402),
             (554,413),(547,413),(547,421),(536,421),(536,427),
             (488,427),(488,421),(477,421),(477,413),(470,413),
             (470,402),(466,402),(466,366),(470,366),(470,355),
             (477,355),(477,347),(488,347)],fill=(29,31,30,255))
  d.polygon([(490,347),(534,347),(534,354),(541,354),(541,362),
             (548,362),(548,371),(551,371),(551,397),(548,397),
             (548,406),(541,406),(541,414),(534,414),(534,420),
             (490,420),(490,414),(483,414),(483,406),(476,406),
             (476,397),(473,397),(473,371),(476,371),(476,362),
             (483,362),(483,354),(490,354)],fill=(0,0,0,0))
  d.rectangle((488,423,536,429),fill=(30,32,31,255))
  d.rectangle((511,383,513,385),fill=(235,43,30,255))
 elif kind=='akiron':
  # One low, wide rear notch with a stepped rounded cut-out. The centered
  # front blade meets the crosshair; no extra protective prongs are added.
  d.rectangle((456,347,568,424),fill=(0,0,0,0))
  d.polygon([(462,425),(462,385),(468,381),(494,381),(494,388),
             (498,395),(503,399),(508,401),(516,401),(521,399),
             (526,395),(530,388),(530,381),(556,381),(562,385),
             (562,425)],fill=(29,31,31,255))
  # Thin edge picks out the inner U without turning it into a second frame.
  d.line([(494,388),(498,395),(503,399),(508,401),(516,401),
          (521,399),(526,395),(530,388)],fill=(65,68,65,255),width=2)
  d.rectangle((486,422,538,427),fill=(29,31,31,255))
  d.rectangle((511,384,513,402),fill=(19,21,20,255))
 elif kind=='eagle':
  # Wide, low-profile rear notch with a centered front blade for the Desert
  # Eagle. The coloured slide below is projected from the original KV6.
  d.rectangle((482,363,542,425),fill=(0,0,0,0))
  d.polygon([(482,425),(482,379),(487,379),(487,371),(494,371),(494,407),
             (530,407),(530,371),(537,371),(537,379),(542,379),(542,425)],
            fill=(29,30,29,255))
  d.rectangle((510,384,514,407),fill=(25,25,24,255))
 elif kind=='m9':
  # Compact M9 rear notch and front blade, above the original voxel slide.
  d.rectangle((481,365,543,425),fill=(0,0,0,0))
  d.polygon([(482,424),(482,382),(488,376),(497,376),(497,405),
             (527,405),(527,376),(536,376),(542,382),(542,424)],
            fill=(28,29,29,255))
  d.rectangle((482,424,542,446),fill=(25,26,26,255))
  d.rectangle((510,384,514,405),fill=(21,22,22,255))
 elif kind=='pistol':
  # Low pistol posts and a contrasting front blade, kept in the voxel style.
  d.rectangle((486,366,538,423),fill=(0,0,0,0))
  d.rectangle((486,375,492,420),fill=(22,25,38,255))
  d.rectangle((532,375,538,420),fill=(22,25,38,255))
  d.rectangle((493,407,531,423),fill=(24,28,45,255))
  d.rectangle((510,384,514,407),fill=(33,37,52,255))
 elif kind=='uzi':
  # The open Uzi front post is protected by two short square ears.
  d.rectangle((486,355,538,423),fill=(0,0,0,0))
  d.rectangle((486,364,492,423),fill=(29,29,29,255))
  d.rectangle((532,364,538,423),fill=(29,29,29,255))
  d.rectangle((493,406,531,423),fill=(38,38,38,255))
  d.rectangle((510,384,514,406),fill=(23,23,23,255))

 else:
  d.rectangle((487,355,537,423),fill=(0,0,0,0))
  d.rectangle((487,359,492,423),fill=(32,34,34,255));d.rectangle((532,359,537,423),fill=(32,34,34,255))
  d.rectangle((493,402,531,423),fill=(40,42,42,255))
  d.line((512,384,512,402),fill=(24,25,25,255),width=1)
 path=OUT/'png'/f'{name}.png';path.parent.mkdir(parents=True,exist_ok=True);img.save(path)
 bg=Image.new('RGBA',img.size,(149,181,185,255)); bd=ImageDraw.Draw(bg)
 bd.rectangle((0,490,1024,768),fill=(124,145,113,255))
 bd.polygon([(0,520),(220,300),(450,520)],fill=(115,145,130,255))
 bd.polygon([(390,520),(760,265),(1024,520)],fill=(105,138,125,255))
 bg.alpha_composite(img);bg.convert('RGB').save(PREVIEWS/(name.replace(' ','-')+'-sight.png'))

def find(z,tail):
 matches=[n for n in z.namelist() if n.lower().endswith('/'+tail.lower())]
 return matches[0] if matches else None

for archive,category,name,kind in PACKS:
 with ZipFile(SOURCES/archive) as z:
  model=find(z,f'kv6/{category}.kv6');assert model
  model_bytes=z.read(model);dims,old=load_kv6(model_bytes);vox=render_voxels(old)
  asset=f'{category}({name})'
  dest=OUT/'kv6'/f'{asset}.kv6';dest.parent.mkdir(parents=True,exist_ok=True);dest.write_bytes(model_bytes)
  preview_model(name,dims,vox)
  if kind:
   original_sight=find(z,f'png/{category}.png')
   if original_sight and kind not in ('eagle','m9'):
    dest=OUT/'png'/f'{asset}.png';dest.parent.mkdir(parents=True,exist_ok=True);dest.write_bytes(z.read(original_sight))
    img=Image.open(dest).convert('RGBA')
    bg=Image.new('RGBA',img.size,(149,181,185,255));bd=ImageDraw.Draw(bg)
    bd.rectangle((0,int(img.height*.64),img.width,img.height),fill=(124,145,113,255))
    bg.alpha_composite(img);bg.convert('RGB').save(PREVIEWS/(asset.replace(' ','-')+'-sight.png'))
   else:
    sight(asset,kind,dims,surface(vox))
  for base in ((['semishoot','semireload'] if category=='semi' else ['smgshoot','smgreload']) if category!='grenade' else []):
   src=find(z,'wav/'+base+'.wav')
   if src:
    dest=OUT/'wav'/f'{base}({name}).wav';dest.parent.mkdir(parents=True,exist_ok=True);dest.write_bytes(z.read(src))
  print(f'{name}: original KV6' + (' and original sight' if kind and original_sight and kind not in ('eagle','m9') else ' and new sight' if kind else ''))
