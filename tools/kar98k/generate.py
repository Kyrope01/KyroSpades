#!/usr/bin/env python3
"""Generate the replacement Kar-98K skin. Requires Pillow; run from repo root."""
import math, random, re, struct
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont
random.seed(98)
ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'resources.override'
name='Kar-98K'
X,Y,Z=27,144,48
vox={}
def put(x,y,z,c):
 if 0<=x<X and 0<=y<Y and 0<=z<Z: vox[x,y,z]=c
def box(x1,x2,y1,y2,z1,z2,c):
 for x in range(x1,x2+1):
  for y in range(y1,y2+1):
   for z in range(z1,z2+1): put(x,y,z,c)
def cylinder(y1,y2,cx,cz,r,c):
 for y in range(y1,y2+1):
  for x in range(int(cx-r),int(cx+r)+1):
   for z in range(int(cz-r),int(cz+r)+1):
    if (x-cx)**2+(z-cz)**2 <= r*r+0.3: put(x,y,z,c)
# File Y runs from butt to muzzle; file Z points downward. At 144 voxels
# long this is automatically rendered at 0.0125 world units per voxel.
wood=(123,65,38); steel=(55,58,56); dark=(32,34,33); edge=(81,84,80)
# Reference-inspired continuous stock, with a flared butt, narrow wrist and
# long tapering fore-end. Cross-sections are rounded rather than box-shaped.
def interp(y, points):
 for (a,va),(b,vb) in zip(points,points[1:]):
  if y <= b: return va+(vb-va)*(y-a)/(b-a)
 return points[-1][1]
tops=[(0,25),(10,24),(24,22),(31,23),(39,22),(49,20),(65,20),(95,21),(124,21)]
bottoms=[(0,43),(8,42),(21,37),(29,33),(35,31),(41,31),(48,33),(60,32),(90,30),(115,29),(124,28)]
widths=[(0,4.5),(12,5.5),(25,4.5),(35,3.2),(49,5),(75,4.5),(112,3.5),(124,3)]
for y in range(125):
 top=interp(y,tops); bottom=interp(y,bottoms); width=interp(y,widths)
 mid=(top+bottom)/2; half=(bottom-top)/2
 for x in range(3,22):
  for z in range(17,45):
   # Rounded edges and subtle walnut grain. Grain follows the long axis.
   if ((x-12)/width)**2 + ((z-mid)/(half+0.5))**4 <= 1.04:
    grain=2.8*math.sin(y*.16+z*.75)+1.6*math.sin(y*.41-z*.37)+1.5*math.sin(z*2.1+y*.04)
    bevel=-5 if abs(x-12)>width-.9 else 0
    put(x,y,z,tuple(max(1,int(v+grain+bevel)) for v in wood))
# Cupped butt plate follows the stock profile.
for y in (0,1):
 for x in range(X):
  for z in range(Z):
   if (x,y,z) in vox: put(x,y,z,steel if y==0 else dark)
# Inletted sling slot and circular stock fitting on both visible sides.
for x in (7,17):
 for y in range(10,17):
  for z in range(31,34): put(x,y,z,dark)
 for y in range(20,24):
  for z in range(34,38):
   if (y-21.5)**2+(z-35.5)**2<4: put(x,y,z,edge)
 put(x,21,35,dark)
# Blued receiver, barrel and front muzzle. The long wooden upper handguard
# sits below the rear tangent sight, as in the supplied references.
cylinder(45,67,12,20,3.0,steel)
cylinder(63,143,12,19,2.0,steel)
for y in range(67,111):
 for x in range(9,16):
  for z in range(20,23): put(x,y,z,(92,49,29))
# Highlight the receiver top and a small ejection-port recess.
box(11,13,47,60,17,17,edge)
box(13,14,52,60,17,18,dark)
# Rear tangent sight ramp and its raised leaf.
box(10,14,70,82,16,17,steel)
for y in range(71,81):
 for x in range(11,14): put(x,y,15-(y-71)//5,edge)
box(11,13,76,79,12,13,dark)
# Front sight blade and low protective base.
box(10,14,137,140,16,17,steel)
box(11,13,138,139,12,15,dark)
# Barrel bands curve around the fore-end. Two strips are close together near
# the muzzle end, with a subtly brighter upper edge.
for y in list(range(108,111))+list(range(121,124)):
 for x in range(8,17):
  for z in range(19,30):
   if (x,y,z) in vox or (x-12)**2/18+(z-24)**2/28 <= 1:
    put(x,y,z,steel if z>19 else edge)
# Nose cap and the thin, separately visible cleaning rod beneath the barrel.
box(10,14,124,126,22,27,dark)
box(12,12,126,140,27,27,edge)
# Integral floorplate / trigger guard, a thin oval loop below the action.
box(10,14,49,64,32,32,steel)
for y in range(46,66):
 for z in range(32,40):
  q=((y-55.5)/10)**2+((z-34)/5.5)**2
  if .55 < q < 1.25:
   for x in (10,14): put(x,y,z,dark)
for z in range(33,37): put(12,55,z,edge)
put(12,54,37,edge)
# Bent bolt on the right, with a rounded dark knob.
box(15,19,49,51,19,21,steel)
for step in range(6):
 box(18+step//2,19+step//2,49,51,21+step,21+step,steel)
for x in range(19,25):
 for y in range(47,53):
  for z in range(25,31):
   if (x-21.5)**2+(y-49.5)**2+(z-27.5)**2 <= 8: put(x,y,z,dark)
# Visible surfaces, in KV6 x/y/z order; z points down in file space.
dirs=[(-1,0,0,1),(1,0,0,2),(0,-1,0,4),(0,1,0,8),(0,0,-1,16),(0,0,1,32)]
surface=[]
for (x,y,z),c in sorted(vox.items()):
 mask=sum(bit for dx,dy,dz,bit in dirs if (x+dx,y+dy,z+dz) not in vox)
 if mask: surface.append((x,y,z,c,mask))
counts={(x,y):0 for x in range(X) for y in range(Y)}
for x,y,z,c,m in surface: counts[x,y]+=1
# Quantize a local outward normal to the game's own KV6 normal table. This
# makes point-mode lighting follow the rounded voxel surfaces as well.
normal_text=(ROOT/'src/model_normals.h').read_text()
normals=[tuple(map(float, match)) for match in re.findall(r'\{\s*(-?\d+\.\d+),\s*(-?\d+\.\d+),\s*(-?\d+\.\d+)\}',normal_text)]
assert len(normals)==256
def normal_index(x,y,z):
 vector=[0.,0.,0.]
 for dx in (-1,0,1):
  for dy in (-1,0,1):
   for dz in (-1,0,1):
    if (dx or dy or dz) and (x+dx,y+dy,z+dz) not in vox:
     weight=1/(dx*dx+dy*dy+dz*dz)
     vector[0]+=dx*weight; vector[1]+=dy*weight; vector[2]+=dz*weight
 return max(range(len(normals)),key=lambda i: sum(a*b for a,b in zip(vector,normals[i])))
with open(OUT/'kv6'/f'semi({name}).kv6','wb') as f:
 f.write(struct.pack('<4sIIIfffI',b'Kvxl',X,Y,Z,12.,78.75,29.25,len(surface)))
 for x,y,z,(r,g,b),m in surface: f.write(struct.pack('<BBBBHBB',b,g,r,128,z,m,normal_index(x,y,z)))
 for x in range(X): f.write(struct.pack('<I',sum(counts[x,y] for y in range(Y))))
 for x in range(X):
  for y in range(Y): f.write(struct.pack('<H',counts[x,y]))
# Render a copy of the voxel rifle looking down its barrel. The PNG-only
# sights have a small square post and simple rectangular protective ears,
# following the classic rifle overlay while keeping the held model unchanged.
ads_vox = vox.copy()
# Narrow the front blade in the ADS copy for a precise central aiming point.
for y in (138,139):
 for x in (11,13):
  for z in range(12,16): ads_vox.pop((x,y,z),None)
# Keep the crosspiece that joins the ADS-only protective ears to the barrel.
for y in range(138,141):
 for x in range(8,17):
  for z in range(16,19): ads_vox[x,y,z] = steel
ads_surface=[]
for (x,y,z),color in sorted(ads_vox.items()):
 mask=sum(bit for dx,dy,dz,bit in dirs if (x+dx,y+dy,z+dz) not in ads_vox)
 if mask: ads_surface.append((x,y,z,color,mask))
# The camera looks at the tip of the front blade.
WADS,HADS=1024,768
im=Image.new('RGBA',(WADS,HADS),(0,0,0,0)); d=ImageDraw.Draw(im)
cam_x,cam_y,cam_z=12.5,-45.,8.
target_y,target_z=138.,12.
focal=2100.
def project(p):
 x,y,z=p
 depth=y-cam_y
 axis_z=cam_z+(target_z-cam_z)*depth/(target_y-cam_y)
 return (round(WADS/2-(x-cam_x)*focal/depth),
         round(HADS/2+(z-axis_z)*focal/depth))
# Visible outer faces in file coordinates. Painter sorting far-to-near works
# here because the camera is outside the model looking along its long axis.
faces=[
 (-1,0,0,1,[(0,0,0),(0,1,0),(0,1,1),(0,0,1)],.80),
 (1,0,0,2,[(1,0,0),(1,0,1),(1,1,1),(1,1,0)],.85),
 (0,-1,0,4,[(0,0,0),(0,0,1),(1,0,1),(1,0,0)],.75),
 (0,1,0,8,[(0,1,0),(1,1,0),(1,1,1),(0,1,1)],.75),
 (0,0,-1,16,[(0,0,0),(1,0,0),(1,1,0),(0,1,0)],1.18),
 (0,0,1,32,[(0,0,1),(0,1,1),(1,1,1),(1,0,1)],.65)]
polygons=[]
for x,y,z,color,mask in ads_surface:
 for nx,ny,nz,bit,corners,light in faces:
  if not mask & bit: continue
  cx=x+(1 if nx>0 else 0 if nx<0 else .5)
  cy=y+(1 if ny>0 else 0 if ny<0 else .5)
  cz=z+(1 if nz>0 else 0 if nz<0 else .5)
  if nx*(cam_x-cx)+ny*(cam_y-cy)+nz*(cam_z-cz)<=0: continue
  pts=[project((x+dx,y+dy,z+dz)) for dx,dy,dz in corners]
  col=tuple(min(255,int(v*light)) for v in color)+(255,)
  polygons.append((cy,pts,col))
for _,points,color in sorted(polygons,key=lambda item:item[0],reverse=True):
 d.polygon(points,fill=color)
# Shorten the tall black sight base by compressing its projected vertical band
# and moving the receiver below it up by the same amount. Keep the front
# sight itself at screen center and extend the last row to the canvas edge.
base_top,base_bottom,removed=405,440,12
body=im.copy()
im.paste(body.crop((0,base_top,WADS,base_bottom)).resize(
 (WADS,base_bottom-base_top-removed),Image.Resampling.NEAREST),(0,base_top))
im.paste(body.crop((0,base_bottom,WADS,HADS)),(0,base_bottom-removed))
im.paste(body.crop((0,HADS-1,WADS,HADS)).resize((WADS,removed),Image.Resampling.NEAREST),
 (0,HADS-removed))
d=ImageDraw.Draw(im)
# Remove the underlying voxel crosspiece outside the narrower sight guards.
# The guards themselves are drawn immediately afterwards, unchanged.
d.rectangle((450,420,466,452),fill=(0,0,0,0))
d.rectangle((558,420,574,452),fill=(0,0,0,0))
# Rectangular guards rendered in the same voxel style. Relative to the
# previous 25x70-pixel guards, these are 20x63 (80% wide, 90% tall).
for x in (467,538):
 d.rectangle((x,402,x+19,464),fill=(41,43,42,255))
# The front sight is a hollow rectangular frame with a narrow post rising
# from its bottom. The post's top pixel is the exact center/point of aim.
# Clear the projected model behind the aperture so it remains see-through.
d.rectangle((507,374,517,395),fill=(0,0,0,0))
# Two opaque pixels per outer edge keep both sides visible when the HUD
# downsamples this image; retain a hollow opening and a one-pixel post.
d.rectangle((507,374,517,395),outline=(25,27,26,255),width=2)
d.rectangle((509,376,515,393),outline=(114,117,111,255),width=1)
d.line((512,384,512,394),fill=(25,27,26,255),width=1)
im.save(OUT/'png'/f'semi({name}).png')
# Render an orthographic side view directly from the generated voxel data.
W,H=1600,740
preview=Image.new('RGB',(W,H),(24,28,30)); dr=ImageDraw.Draw(preview)
try: font=ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf',27); small=ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf',18)
except OSError: font=small=ImageFont.load_default()
dr.text((45,30),'KAR-98K  /  VOXEL MODEL',font=font,fill=(220,217,204))
dr.text((45,75),'Orthographic side view • generated from the actual KV6 voxels',font=small,fill=(145,155,153))
p=9; ox=150; oy=130
for y in range(Y):
 for z in range(Z):
  candidates=[(x,vox[x,y,z]) for x in range(X) if (x,y,z) in vox]
  if candidates:
   x,col=max(candidates); xx=ox+y*p; zz=oy+z*p
   dr.rectangle((xx,zz,xx+p-1,zz+p-1),fill=col)
   dr.line((xx,zz,xx+p-1,zz),fill=tuple(min(255,int(v*1.1)) for v in col),width=1)
dr.text((45,690),'Unscoped reference-inspired rifle  •  144 voxels long  •  automatically scaled',font=small,fill=(160,168,162))
preview.save(ROOT/'docs/previews/kar98k-model.png')
# Composite a neutral scene behind the transparent ADS art for review only.
bg=Image.new('RGBA',(WADS,HADS),(149,181,185,255)); bd=ImageDraw.Draw(bg)
bd.rectangle((0,490,WADS,HADS),fill=(124,145,113,255))
bd.polygon([(0,520),(220,300),(450,520)],fill=(115,145,130,255))
bd.polygon([(390,520),(760,265),(1024,520)],fill=(105,138,125,255))
bg.alpha_composite(im); bg.convert('RGB').save(ROOT/'docs/previews/kar98k-scope.png')
print('surface voxels:',len(surface))
