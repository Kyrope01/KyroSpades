#!/usr/bin/env python3
"""Generate a reference-inspired SCAR-H SMG skin, voxel ADS and previews.

Requires Pillow. The geometry and sight are original; the sight body is
projected from these same voxels. Run from any working directory.
"""
import math
import re
import struct
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[2]
DEST = ROOT / 'resources.override/kv6/smg(SCAR-H).kv6'
PREVIEW = ROOT / 'docs/previews/scarh-model.png'
X, Y, Z = 23, 144, 55
vox = {}
tan = (151, 134, 104)
lighttan = (171, 153, 118)
darktan = (113, 100, 80)
black = (35, 38, 37)
steel = (59, 62, 59)
edge = (83, 84, 77)

def put(x, y, z, color):
    if 0 <= x < X and 0 <= y < Y and 0 <= z < Z:
        vox[x,y,z] = color

def box(x0,x1,y0,y1,z0,z1,color):
    for x in range(x0,x1+1):
        for y in range(y0,y1+1):
            for z in range(z0,z1+1): put(x,y,z,color)

def tube(y0,y1,cx,cz,r,color):
    for y in range(y0,y1+1):
        for x in range(math.floor(cx-r),math.ceil(cx+r)+1):
            for z in range(math.floor(cz-r),math.ceil(cz+r)+1):
                if (x-cx)**2 + (z-cz)**2 <= r*r+.2: put(x,y,z,color)

# Stock points to y=0, muzzle to y=143; file z increases downward.
# Extended adjustable SCAR stock with rubber buttpad and angled lower edge.
for y in range(2,43):
    top = 19 if y < 31 else 18
    bottom = round(31 - min(y,31)*.13)
    for z in range(top,bottom+1):
        for x in range(7,16):
            if x in (7,15) and z in (top,bottom): continue
            put(x,y,z,tan if z < 27 else darktan)
box(6,16,0,3,18,34,black)             # butt pad
box(7,15,4,5,19,31,edge)
box(7,15,17,37,17,19,darktan)          # cheekpiece
box(8,14,20,35,16,17,tan)
box(7,15,37,42,19,31,darktan)          # folding hinge
box(6,16,40,42,19,30,edge)
# Upper and lower receiver with bevels, long top rail and visible side details.
box(6,16,43,104,19,29,tan)
box(7,15,43,104,17,18,lighttan)
box(7,15,43,82,30,34,darktan)
box(7,15,83,103,30,32,tan)
box(7,15,41,107,15,16,black)
for y in range(42,108,3): box(7,15,y,y,14,14,edge)
# Receiver panel seam, ejection port, charging handle and safety pins.
box(17,17,49,82,20,20,darktan)
box(17,17,56,77,22,24,black)
box(17,17,58,75,22,22,edge)
box(18,19,72,77,25,25,steel)
for y,z in ((50,27),(56,31),(72,30),(82,27)):
    box(17,17,y,y+1,z,z+1,black)
box(17,17,84,98,23,25,darktan)
# Handguard air slots and side rail, like the supplied profile.
for y in range(86,104,5): box(17,17,y,y+2,20,20,black)
box(17,18,86,103,27,28,black)
for y in range(87,103,3): box(18,18,y,y,26,29,edge)
# Trigger guard and central trigger.
for y in range(59,77):
    for z in range(33,43):
        r=((y-68)/9)**2+((z-36.5)/5.5)**2
        if .58 < r < 1.3:
            for x in (8,14): put(x,y,z,black)
box(11,11,66,66,34,39,edge)
# Pistol grip slopes toward the stock.
for z in range(32,53):
    cy=round(57-(z-32)*.35)
    for y in range(cy-4,cy+4):
        for x in range(7,16):
            if x in (7,15) and z in (32,52): continue
            put(x,y,z, darktan if z%5 else tan)
box(7,15,46,53,52,53,black)
# Characteristically straight, broad 7.62 magazine, sloping slightly forward.
for z in range(32,51):
    offset=round((z-32)*.12)
    for y in range(77+offset,89+offset):
        for x in range(7,16):
            if x in (7,15) and (y==77+offset or y==88+offset): continue
            put(x,y,z,darktan if y%4==0 else tan)
box(7,15,79,92,50,51,darktan)
# Narrow barrel, gas block and flash hider.
tube(105,143,11,21,2.0,black)
box(8,14,105,110,18,25,steel)
tube(137,143,11,21,2.6,steel)
for y in (139,141):
    box(10,12,y,y,18,18,black)
box(10,12,143,143,20,22,(19,21,20))
# Iron sight structures, front wings and rear adjustment wheels.
box(10,12,108,111,12,17,steel)
box(8,8,108,111,9,16,black); box(14,14,108,111,9,16,black)
box(11,11,109,110,9,13,edge)
box(9,13,47,51,10,15,black)
box(6,8,47,51,11,15,steel); box(14,16,47,51,11,15,steel)
# Write KV6 surface voxels in loader-compatible x, y, ascending-file-z order.
dirs=[(-1,0,0,1),(1,0,0,2),(0,-1,0,4),(0,1,0,8),(0,0,-1,16),(0,0,1,32)]
surface=[]
for (x,y,z),color in sorted(vox.items()):
    mask=sum(bit for dx,dy,dz,bit in dirs if (x+dx,y+dy,z+dz) not in vox)
    if mask: surface.append((x,y,z,color,mask))
counts={(x,y):0 for x in range(X) for y in range(Y)}
for x,y,z,color,mask in surface: counts[x,y]+=1
normal_text=(ROOT/'src/model_normals.h').read_text()
normals=[tuple(map(float,m)) for m in re.findall(r'\{\s*(-?\d+\.\d+),\s*(-?\d+\.\d+),\s*(-?\d+\.\d+)\}',normal_text)]
assert len(normals)==256

def normal_index(x,y,z):
    vector=[0.,0.,0.]
    for dx in (-1,0,1):
        for dy in (-1,0,1):
            for dz in (-1,0,1):
                if (dx or dy or dz) and (x+dx,y+dy,z+dz) not in vox:
                    weight=1/(dx*dx+dy*dy+dz*dz)
                    vector[0]+=dx*weight; vector[1]+=dy*weight; vector[2]+=dz*weight
    return max(range(256), key=lambda i: sum(a*b for a,b in zip(vector,normals[i])))

DEST.parent.mkdir(parents=True,exist_ok=True)
with DEST.open('wb') as f:
    f.write(struct.pack('<4sIIIfffI',b'Kvxl',X,Y,Z,8.,70.,29.,len(surface)))
    for x,y,z,(r,g,b),mask in surface:
        f.write(struct.pack('<BBBBHBB',b,g,r,128,z,mask,normal_index(x,y,z)))
    for x in range(X): f.write(struct.pack('<I',sum(counts[x,y] for y in range(Y))))
    for x in range(X):
        for y in range(Y): f.write(struct.pack('<H',counts[x,y]))

# Orthographic projection of voxel data.
img=Image.new('RGB',(1600,740),(24,28,30)); draw=ImageDraw.Draw(img)
try:
    title=ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf',27)
    small=ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf',18)
except OSError: title=small=ImageFont.load_default()
draw.text((45,30),'SCAR-H  /  VOXEL MODEL',font=title,fill=(220,217,204))
draw.text((45,75),'Extended stock  /  orthographic projection of the actual KV6',font=small,fill=(145,155,153))
p,ox,oy=9,150,150
for y in range(Y):
    for z in range(Z):
        candidates=[(x,vox[x,y,z]) for x in range(X) if (x,y,z) in vox]
        if candidates:
            _,col=max(candidates)
            draw.rectangle((ox+y*p,oy+z*p,ox+(y+1)*p-1,oy+(z+1)*p-1),fill=col)
draw.text((45,690),'144-voxel length  /  tan receiver  /  top rail  /  7.62 magazine',font=small,fill=(160,168,162))
PREVIEW.parent.mkdir(parents=True,exist_ok=True); img.save(PREVIEW)

# Actual model projected from the rear, then sight-only voxel-style details
# (more precise than the held model) added from the provided ADS references.
ads=Image.new('RGBA',(1024,768),(0,0,0,0)); draw=ImageDraw.Draw(ads)
# KV6 cells occupy [x,x+1]: the receiver spans x=6..16, so its
# geometric centre is 11.5 rather than the centre voxel index 11.
cam_x,cam_y,cam_z=11.5,-36.,7.
target_y,target_z=110.,10.
focal=1900.
def project(p):
    x,y,z=p; depth=y-cam_y
    axis_z=cam_z+(target_z-cam_z)*depth/(target_y-cam_y)
    return (round(512-(x-cam_x)*focal/depth),round(384+(z-axis_z)*focal/depth))
faces=[(-1,0,0,1,[(0,0,0),(0,1,0),(0,1,1),(0,0,1)],.80),
       (1,0,0,2,[(1,0,0),(1,0,1),(1,1,1),(1,1,0)],.85),
       (0,-1,0,4,[(0,0,0),(0,0,1),(1,0,1),(1,0,0)],.75),
       (0,1,0,8,[(0,1,0),(1,1,0),(1,1,1),(0,1,1)],.75),
       (0,0,-1,16,[(0,0,0),(1,0,0),(1,1,0),(0,1,0)],1.18),
       (0,0,1,32,[(0,0,1),(0,1,1),(1,1,1),(1,0,1)],.65)]
polygons=[]
for x,y,z,color,mask in surface:
    for nx,ny,nz,bit,corners,brightness in faces:
        if not mask&bit: continue
        cx=x+(1 if nx>0 else 0 if nx<0 else .5)
        cy=y+(1 if ny>0 else 0 if ny<0 else .5)
        cz=z+(1 if nz>0 else 0 if nz<0 else .5)
        if nx*(cam_x-cx)+ny*(cam_y-cy)+nz*(cam_z-cz)<=0: continue
        pts=[project((x+dx,y+dy,z+dz)) for dx,dy,dz in corners]
        col=tuple(min(255,int(v*brightness)) for v in color)+(255,)
        polygons.append((cy,pts,col))
for _,pts,col in sorted(polygons,key=lambda item:item[0],reverse=True): draw.polygon(pts,fill=col)
# Front aperture: blocky stepped arch with translucent-free opening, flanking
# vertical wings and one-pixel post. Tip is exactly screen centre.
draw.rectangle((477,350,547,421),fill=(0,0,0,0))
draw.rectangle((477,353,483,420),fill=(51,54,52,255))
draw.rectangle((541,353,547,420),fill=(51,54,52,255))
draw.rectangle((484,365,540,420),fill=(33,35,34,255))
draw.rectangle((489,359,535,365),fill=(33,35,34,255))
draw.rectangle((497,355,527,358),fill=(33,35,34,255))
# Circular aperture drawn on a pixel grid, remaining fully transparent.
draw.ellipse((495,366,529,400),fill=(0,0,0,0))
draw.line((512,384,512,399),fill=(27,29,28,255),width=1)
draw.rectangle((484,402,540,411),fill=(79,80,73,255))
draw.rectangle((484,412,540,421),fill=(38,40,38,255))
ADS=ROOT/'resources.override/png/smg(SCAR-H).png'
ADS.parent.mkdir(parents=True,exist_ok=True); ads.save(ADS)
bg=Image.new('RGBA',ads.size,(149,181,185,255)); d=ImageDraw.Draw(bg)
d.rectangle((0,490,1024,768),fill=(124,145,113,255))
d.polygon([(0,520),(220,300),(450,520)],fill=(115,145,130,255))
d.polygon([(390,520),(760,265),(1024,520)],fill=(105,138,125,255))
bg.alpha_composite(ads); bg.convert('RGB').save(ROOT/'docs/previews/scarh-sight.png')
print(f'{DEST}: {len(surface)} surface voxels; model and sight previews generated')
