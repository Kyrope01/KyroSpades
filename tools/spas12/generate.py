#!/usr/bin/env python3
"""Generate the unfolded-stock SPAS-12-inspired shotgun KV6 and side preview.

Run from any directory with Python and Pillow installed. All geometry is
original and based on the user's reference photograph, not a downloaded mesh.
"""
import math
import re
import struct
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[2]
DEST = ROOT / 'resources.override/kv6/shotgun(SPAS-12).kv6'
PREVIEW = ROOT / 'docs/previews/spas12-model.png'
X, Y, Z = 22, 112, 49
vox = {}
steel = (62, 65, 65)
dark = (32, 35, 36)
mid = (49, 52, 53)
light = (91, 93, 90)
rubber = (40, 42, 42)

def put(x,y,z,color):
    if 0 <= x < X and 0 <= y < Y and 0 <= z < Z:
        vox[x,y,z] = color

def box(x0,x1,y0,y1,z0,z1,color):
    for x in range(x0,x1+1):
        for y in range(y0,y1+1):
            for z in range(z0,z1+1):
                put(x,y,z,color)

def tube(y0,y1,cx,cz,r,color):
    for y in range(y0,y1+1):
        for x in range(math.floor(cx-r),math.ceil(cx+r)+1):
            for z in range(math.floor(cz-r),math.ceil(cz+r)+1):
                if (x-cx)**2 + (z-cz)**2 <= r*r+.2:
                    put(x,y,z,color)

def rail(y0,z0,y1,z1,thickness,color):
    steps=max(abs(y1-y0),abs(z1-z0))*2
    for t in range(steps+1):
        y=round(y0+(y1-y0)*t/steps)
        z=round(z0+(z1-z0)*t/steps)
        box(10-thickness,10+thickness,y,y,z-thickness,z+thickness,color)

# The 112-voxel model is automatically fitted to 36 legacy shotgun voxels.
# y=0 is the stock, y=111 is the muzzle; z points down.  The pivot keeps the
# hand location near the legacy shotgun's original pivot after scaling.
# Distinctive folding stock, depicted extended as in the lower reference.
box(7,13,0,3,20,31,dark)                 # broad buttplate
box(8,12,4,8,20,22,steel)
box(8,12,4,8,29,31,steel)
rail(7,21,31,23,1,steel)                # upper strut
rail(7,30,31,27,1,steel)                # lower angled strut
rail(11,22,26,28,0,light)               # internal skeleton bar
box(8,12,29,34,20,29,mid)               # folding hinge block
for x in (7,13):
    for y in range(30,34):
        for z in range(22,26):
            if (y-31.5)**2+(z-23.5)**2 < 4: put(x,y,z,light)
    put(x,31,23,dark)

# Main receiver with a bevel, upper sight rail and inset ejection port.
box(6,14,33,58,19,29,steel)
box(7,13,34,57,17,18,mid)
box(7,13,34,57,30,31,mid)
for y in range(35,57):
    for z in (19,29):
        put(6,y,z,mid); put(14,y,z,mid)
box(15,15,42,51,21,25,dark)
box(15,15,43,50,21,21,light)
box(15,15,43,43,22,24,light)
box(15,15,48,49,23,24,(78,80,78))
# Trigger guard loop and a distinct central trigger.
for y in range(43,57):
    for z in range(30,39):
        r=((y-50)/7)**2+((z-33.4)/5.4)**2
        if .58 < r < 1.28:
            for x in (8,12): put(x,y,z,dark)
box(10,10,49,49,31,35,light)
put(10,48,36,light)
# Pistol grip angles back from the receiver and has dark moulded texture.
for z in range(29,47):
    center=40-(z-29)*.38
    for y in range(round(center)-3,round(center)+4):
        for x in range(7,14):
            if abs(x-10)==3 and z in (29,46): continue
            shade=4 if z%4==0 else 0
            put(x,y,z,tuple(v+shade for v in rubber))
box(7,13,30,35,46,47,dark)
# Barrel and a shorter magazine tube beneath it, with a visible muzzle.
tube(54,111,10,19,2.4,steel)
tube(63,102,10,27,2.1,mid)
tube(105,111,10,19,2.6,dark)
# Muzzle bore is a dark face (never pure black, which means team colour).
for x in range(9,12):
    for z in range(18,21): put(x,111,z,(17,19,19))
# The wide, ribbed sliding fore-end and its end collars.
for y in range(59,85):
    for x in range(5,16):
        for z in range(21,33):
            if ((x-10)/5.1)**2 + ((z-26.5)/6.1)**2 <= 1.05:
                col=(57,59,58) if y%3==0 else (38,40,40)
                put(x,y,z,col)
for y in (58,59,84,85):
    for x in range(6,15):
        for z in range(21,32):
            if ((x-10)/4.4)**2+((z-26)/5.4)**2<=1:
                put(x,y,z,steel)
# Front/rear sight blades and small magazine cap.
box(9,11,51,54,14,17,dark)
box(9,11,52,53,13,13,light)
box(9,11,104,107,14,17,steel)
box(10,10,105,106,12,13,light)
tube(102,104,10,27,2.2,dark)

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
    f.write(struct.pack('<4sIIIfffI',b'Kvxl',X,Y,Z,7.,51.3333,32.,len(surface)))
    for x,y,z,(r,g,b),mask in surface:
        f.write(struct.pack('<BBBBHBB',b,g,r,128,z,mask,normal_index(x,y,z)))
    for x in range(X): f.write(struct.pack('<I',sum(counts[x,y] for y in range(Y))))
    for x in range(X):
        for y in range(Y): f.write(struct.pack('<H',counts[x,y]))

# Side projection from the actual voxel model, not an illustrative redraw.
W,H=1600,740
img=Image.new('RGB',(W,H),(24,28,30)); draw=ImageDraw.Draw(img)
try:
    title=ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf',27)
    small=ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf',18)
except OSError:
    title=small=ImageFont.load_default()
draw.text((45,30),'SPAS-12  /  VOXEL MODEL',font=title,fill=(220,217,204))
draw.text((45,75),'Unfolded stock • orthographic projection of the actual KV6',font=small,fill=(145,155,153))
p,ox,oy=12,128,125
for y in range(Y):
    for z in range(Z):
        candidates=[(x,vox[x,y,z]) for x in range(X) if (x,y,z) in vox]
        if candidates:
            x,col=max(candidates)
            draw.rectangle((ox+y*p,oy+z*p,ox+(y+1)*p-1,oy+(z+1)*p-1),fill=col)
draw.text((45,690),'112-voxel length  •  folding stock  •  ribbed fore-end  •  separate barrel and magazine tube',font=small,fill=(160,168,162))
PREVIEW.parent.mkdir(parents=True,exist_ok=True)
img.save(PREVIEW)
# First-person ADS overlay: project the actual model from behind its sights.
# The front sight is augmented in the PNG, while the KV6 remains unchanged.
# The 4:3 RGBA image has transparent edges for the HUD's widescreen extension.
ads=Image.new('RGBA',(1024,768),(0,0,0,0)); draw=ImageDraw.Draw(ads)
cam_x,cam_y,cam_z=10.5,-35.,8.
target_y,target_z=105.,12.
focal=1900.
def project(p):
    x,y,z=p
    depth=y-cam_y
    axis_z=cam_z+(target_z-cam_z)*depth/(target_y-cam_y)
    return (round(512-(x-cam_x)*focal/depth), round(384+(z-axis_z)*focal/depth))
faces=[
    (-1,0,0,1,[(0,0,0),(0,1,0),(0,1,1),(0,0,1)],.80),
    (1,0,0,2,[(1,0,0),(1,0,1),(1,1,1),(1,1,0)],.85),
    (0,-1,0,4,[(0,0,0),(0,0,1),(1,0,1),(1,0,0)],.75),
    (0,1,0,8,[(0,1,0),(1,1,0),(1,1,1),(0,1,1)],.75),
    (0,0,-1,16,[(0,0,0),(1,0,0),(1,1,0),(0,1,0)],1.18),
    (0,0,1,32,[(0,0,1),(0,1,1),(1,1,1),(1,0,1)],.65),
]
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
for _,points,color in sorted(polygons,key=lambda item:item[0],reverse=True):
    draw.polygon(points,fill=color)
# Blocky, square-cornered hood inspired by the reference. The aperture itself
# is transparent, and the fine post is one pixel wide with its tip at the
# screen center. The dark base ties the sight into the projected barrel.
draw.rectangle((487,365,537,422),fill=(0,0,0,0))
draw.polygon([(487,373),(493,373),(493,365),(531,365),(531,373),(537,373),
              (537,422),(487,422)],fill=(29,31,31,255))
draw.polygon([(494,376),(498,376),(498,372),(526,372),(526,376),(530,376),
              (530,401),(494,401)],fill=(0,0,0,0))
draw.rectangle((493,402,531,422),fill=(42,44,43,255))
# Shorten only the pin, then shift the complete overlay up by 8 pixels so
# its new tip remains at the screen center. The hood keeps its exact shape.
draw.line((512,392,512,402),fill=(24,26,26,255),width=1)
shifted=Image.new('RGBA',ads.size,(0,0,0,0))
shifted.paste(ads.crop((0,8,1024,768)),(0,0))
shifted.paste(ads.crop((0,767,1024,768)).resize((1024,8),Image.Resampling.NEAREST),(0,760))
ads=shifted
ADS=ROOT/'resources.override/png/shotgun(SPAS-12).png'
ADS.parent.mkdir(parents=True,exist_ok=True)
ads.save(ADS)
# Preview only: a neutral landscape makes the transparent field visible.
background=Image.new('RGBA',ads.size,(149,181,185,255)); bd=ImageDraw.Draw(background)
bd.rectangle((0,490,1024,768),fill=(124,145,113,255))
bd.polygon([(0,520),(220,300),(450,520)],fill=(115,145,130,255))
bd.polygon([(390,520),(760,265),(1024,520)],fill=(105,138,125,255))
background.alpha_composite(ads)
background.convert('RGB').save(ROOT/'docs/previews/spas12-sight.png')
print(f'{DEST}: {len(surface)} surface voxels; previews {PREVIEW} and SPAS-12 sight')
