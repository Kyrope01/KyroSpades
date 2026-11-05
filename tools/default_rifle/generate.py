#!/usr/bin/env python3
"""Generate a detailed version of the bundled default scoped rifle.

Requires Pillow. The optic overlay is derived from resources/png/semi.png;
the KV6 model is an original higher-resolution interpretation of the bundled
44-voxel default rifle. Run from any working directory.
"""
import math, re, struct
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont, ImageChops

ROOT = Path(__file__).resolve().parents[2]
DEST = ROOT/'resources.override/kv6/semi.kv6'
X,Y,Z = 20,176,52
vox = {}
black=(30,31,32); steel=(48,49,49); metal=(79,81,80)
light=(145,146,143); dark=(18,20,21); polymer=(39,40,40)
def put(x,y,z,c):
    if 0<=x<X and 0<=y<Y and 0<=z<Z: vox[x,y,z]=c
def box(x0,x1,y0,y1,z0,z1,c):
    for x in range(x0,x1+1):
        for y in range(y0,y1+1):
            for z in range(z0,z1+1): put(x,y,z,c)
def tube(y0,y1,cx,cz,r,c):
    for y in range(y0,y1+1):
        for x in range(math.floor(cx-r),math.ceil(cx+r)+1):
            for z in range(math.floor(cz-r),math.ceil(cz+r)+1):
                if (x-cx)**2+(z-cz)**2<=r*r+.2: put(x,y,z,c)
# y runs stock to muzzle; file z increases downward. Each length unit
# corresponds to one quarter of a voxel of the bundled default rifle.
# Stock with sloped underside and a silver collar as in the original.
for y in range(0,39):
    top=23
    bottom=46 if y<4 else max(31, round(46-(y-4)*.48))
    for x in range(6,14):
        for z in range(top,bottom+1):
            if x in (6,13) and z in (top,bottom): continue
            put(x,y,z,polymer if z%5 else steel)
box(6,13,0,3,23,46,black)
box(6,13,28,38,23,29,light)
box(7,12,30,36,24,28,(121,123,122))
# Central upper/lower receiver, small panel lines and controls.
box(5,14,39,77,22,33,steel)
box(6,13,39,78,20,21,black)
box(6,13,39,77,34,35,polymer)
box(15,15,48,66,24,27,dark)
box(15,15,49,65,24,24,metal)
box(15,16,67,71,25,26,metal)
for y,z in ((44,29),(51,31),(71,31)):
    box(15,15,y,y+1,z,z+1,light)
# Pistol grip and trigger guard, separate from magazine.
for z in range(33,49):
    cy=round(48-(z-33)*.30)
    for y in range(cy-4,cy+4):
        for x in range(6,14): put(x,y,z,black if z%4 else metal)
for y in range(54,72):
    for z in range(34,43):
        q=((y-63)/9)**2+((z-36)/5)**2
        if .57<q<1.25:
            for x in (7,12): put(x,y,z,dark)
box(9,9,61,61,35,39,metal)
# Square magazine with the distinctive pale lower band of the old model.
box(6,13,64,77,35,48,metal)
box(6,13,64,77,44,49,light)
box(6,13,64,77,50,51,black)
for y in (67,71,75): box(14,14,y,y,36,43,steel)
# Long dark handguard with ventilation slots and upper and lower rails.
box(6,13,78,132,22,32,polymer)
box(7,12,79,132,20,21,black)
box(7,12,79,132,33,34,steel)
for y in range(83,128,8):
    box(14,14,y,y+3,24,25,dark)
    box(14,14,y,y+3,26,26,metal)
for y in range(79,133,4): box(7,12,y,y+1,19,19,metal)
# Slim pale exposed barrel, darker muzzle device.
tube(132,175,9.5,25,2.0,light)
tube(132,136,9.5,25,2.8,steel)
tube(169,175,9.5,25,2.5,black)
for y in (171,173): box(9,10,y,y,23,23,metal)
# Scope mount, cylindrical optic and raised adjustment dial.
box(7,12,48,76,18,19,black)
box(8,11,51,56,15,18,metal); box(8,11,70,74,15,18,metal)
tube(45,82,9.5,10,4.0,black)
tube(44,51,9.5,10,5.0,steel)
tube(77,84,9.5,10,5.0,steel)
for y in (45,50,77,83): tube(y,y,9.5,10,5.3,metal)
# Open the sight right through, instead of leaving the eyepiece capped.
# Recessed inner wall, a tiny crosshair and a pair of subtle lens glints
# are physical voxels inside the bore, not a flat disk painted on its end.
for y in range(44,85):
    for x in range(5,15):
        for z in range(5,16):
            if (x-9.5)**2+(z-10)**2 <= 2.7**2:
                vox.pop((x,y,z),None)
# Dark interior reticle sits deep inside the hollow tube. A clear center gap
# keeps it from resembling the opaque lens cap it replaces.
for x,z in ((9,8),(9,12),(7,10),(8,10),(11,10),(12,10)):
    put(x,65,z,(58,72,73))
put(7,48,9,(74,99,101))
put(12,48,11,(53,74,77))
box(8,11,62,66,3,6,black)
box(8,11,62,66,2,2,metal)
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
    f.write(struct.pack('<4sIIIfffI',b'Kvxl',X,Y,Z,4.,66.,34.,len(surface)))
    for x,y,z,(r,g,b),mask in surface:
        f.write(struct.pack('<BBBBHBB',b,g,r,128,z,mask,normal_index(x,y,z)))
    for x in range(X): f.write(struct.pack('<I',sum(counts[x,y] for y in range(Y))))
    for x in range(X):
        for y in range(Y): f.write(struct.pack('<H',counts[x,y]))

# Orthographic side projection, generated directly from the voxel data.
img=Image.new('RGB',(1600,740),(24,28,30)); d=ImageDraw.Draw(img)
try:
    title=ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf',27)
    small=ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf',18)
except OSError: title=small=ImageFont.load_default()
d.text((45,30),'DEFAULT RIFLE  /  HIGH-RES VOXEL MODEL',font=title,fill=(220,217,204))
d.text((45,75),'Scoped modern rifle • orthographic projection of the actual KV6',font=small,fill=(145,155,153))
p,ox,oy=8,100,140
for y in range(Y):
    for z in range(Z):
        candidates=[(x,vox[x,y,z]) for x in range(X) if (x,y,z) in vox]
        if candidates:
            _,c=max(candidates)
            d.rectangle((ox+y*p,oy+z*p,ox+(y+1)*p-1,oy+(z+1)*p-1),fill=c)
d.text((45,690),'176-voxel length  •  optic  •  ventilated handguard  •  silver barrel',font=small,fill=(160,168,162))
preview=ROOT/'docs/previews/default-rifle-model.png'; preview.parent.mkdir(parents=True,exist_ok=True); img.save(preview)

# Close-up looking into the actual KV6 scope from the rear, with perspective.
# This shows the open bore and recessed reticle rather than a side-only view.
close=Image.new('RGB',(1000,780),(110,146,151)); cd=ImageDraw.Draw(close)
cd.text((32,24),'DEFAULT RIFLE  /  SCOPE MODEL CLOSE-UP',font=title,fill=(235,235,229))
cd.text((32,65),'Voxel projection from behind the optic; background shows through the bore',font=small,fill=(218,227,221))
camera=(12.,-30.,8.); target=(9.5,64.,10.)
def unit(v):
    q=math.sqrt(sum(a*a for a in v)); return tuple(a/q for a in v)
def dot(a,b): return sum(x*y for x,y in zip(a,b))
fwd=unit(tuple(t-c for t,c in zip(target,camera)))
right=unit((fwd[1],-fwd[0],0.))
down=(right[1]*fwd[2],-right[0]*fwd[2],right[0]*fwd[1]-right[1]*fwd[0])
def project_close(p):
    rel=tuple(a-b for a,b in zip(p,camera)); depth=dot(rel,fwd)
    return (round(500+2800*dot(rel,right)/depth),round(450+2800*dot(rel,down)/depth))
faces=[(-1,0,0,1,[(0,0,0),(0,1,0),(0,1,1),(0,0,1)],.80),
       (1,0,0,2,[(1,0,0),(1,0,1),(1,1,1),(1,1,0)],.85),
       (0,-1,0,4,[(0,0,0),(0,0,1),(1,0,1),(1,0,0)],.75),
       (0,1,0,8,[(0,1,0),(1,1,0),(1,1,1),(0,1,1)],.75),
       (0,0,-1,16,[(0,0,0),(1,0,0),(1,1,0),(0,1,0)],1.18),
       (0,0,1,32,[(0,0,1),(0,1,1),(1,1,1),(1,0,1)],.65)]
polygons=[]
for x,y,z,color,mask in surface:
    if not 44<=y<=84 or z>18: continue
    for nx,ny,nz,bit,corners,brightness in faces:
        if not mask&bit: continue
        cx=x+(1 if nx>0 else 0 if nx<0 else .5)
        cy=y+(1 if ny>0 else 0 if ny<0 else .5)
        cz=z+(1 if nz>0 else 0 if nz<0 else .5)
        if nx*(camera[0]-cx)+ny*(camera[1]-cy)+nz*(camera[2]-cz)<=0: continue
        pts=[project_close((x+dx,y+dy,z+dz)) for dx,dy,dz in corners]
        col=tuple(min(255,int(v*brightness)) for v in color)
        depth=dot(tuple(a-b for a,b in zip((cx,cy,cz),camera)),fwd)
        polygons.append((depth,pts,col))
for _,pts,col in sorted(polygons,key=lambda p:p[0],reverse=True): cd.polygon(pts,fill=col)
close.save(ROOT/'docs/previews/default-rifle-model-scope.png')

# Rebuild the optic housing at a finer voxel pitch: preserve the original
# original stepped, straight-sided outline, dark finish, mount and reticle.
# 419x315 pixel geometry is enlarged 4x with nearest-neighbour so the steps
# are actual small voxels, not a larger resample of the old coarse blocks.
source=Image.open(ROOT/'resources/png/semi.png').convert('RGBA')
optic=Image.new('RGBA',(419,315),(0,0,0,0)); d=ImageDraw.Draw(optic)
# Rear mount and squared top cap, behind the finely stepped annular housing.
d.polygon([(129,257),(144,246),(147,236),(272,236),(275,246),(290,257),
           (290,301),(276,301),(272,314),(147,314),(143,301),(129,301)],fill=(22,21,23,255))
d.rectangle((144,272,275,298),fill=(16,15,17,255))
d.polygon([(159,0),(260,0),(260,13),(265,16),(265,43),
           (153,43),(153,16),(159,13)],fill=(18,17,19,255))
# Keep the original straight-sided, chunky stepped silhouette: detail is
# finer on the metal, but it must not become a smooth circular ring.
outer=[(168,12),(251,12),(251,26),(266,26),(266,45),(289,45),
       (289,65),(307,65),(307,88),(326,88),(326,108),(340,108),
       (340,196),(326,196),(326,218),(307,218),(307,240),(289,240),
       (289,262),(266,262),(266,274),(153,274),(153,262),(130,262),
       (130,240),(112,240),(112,218),(92,218),(92,196),(78,196),
       (78,108),(92,108),(92,88),(112,88),(112,65),(130,65),
       (130,45),(153,45),(153,26),(168,26)]
opening=[(176,55),(243,55),(248,61),(248,68),(264,68),(264,77),
         (278,77),(278,90),(294,90),(294,105),(314,105),(314,205),
         (294,205),(294,220),(278,220),(278,234),(264,234),(264,246),
         (248,246),(248,257),(171,257),(171,246),(155,246),(155,234),
         (141,234),(141,220),(125,220),(125,205),(105,205),(105,105),
         (125,105),(125,90),(141,90),(141,77),(155,77),(155,68),
         (171,68),(171,61)]
# Flat dark housing: no inset grey lines or outlines.
d.polygon(outer,fill=(27,26,28,255))
d.polygon(opening,fill=(0,0,0,0))
sight=optic.resize((source.width*2,source.height*2),Image.Resampling.NEAREST)
# Copy only the original central reticle and red dot into the new aperture,
# leaving the housing to the higher-resolution voxel drawing above.
reticle=source.resize(sight.size,Image.Resampling.LANCZOS)
mask=Image.new('L',optic.size,0); md=ImageDraw.Draw(mask)
md.polygon(opening,fill=255)
mask=mask.resize(sight.size,Image.Resampling.NEAREST)
# Extract just the original crosshair bands, not the surrounding housing.
bands=Image.new('L',sight.size,0); bd=ImageDraw.Draw(bands)
bd.rectangle((0,620,sight.width-1,644),fill=255)
bd.rectangle((828,0,848,sight.height-1),fill=255)
mask=ImageChops.multiply(mask,bands)
reticle.putalpha(ImageChops.multiply(reticle.getchannel('A'),mask))
sight.alpha_composite(reticle)
out=ROOT/'resources.override/png/semi.png';out.parent.mkdir(parents=True,exist_ok=True);sight.save(out)
# Neutral preview makes the original transparent opening and red dot visible.
bg=Image.new('RGBA',sight.size,(149,181,185,255)); bd=ImageDraw.Draw(bg)
bd.rectangle((0,int(sight.height*.65),sight.width,sight.height),fill=(124,145,113,255))
bd.polygon([(0,int(sight.height*.7)),(sight.width*.25,sight.height*.35),(sight.width*.5,sight.height*.7)],fill=(115,145,130,255))
bd.polygon([(sight.width*.4,sight.height*.7),(sight.width*.75,sight.height*.3),(sight.width,sight.height*.7)],fill=(105,138,125,255))
bg.alpha_composite(sight); bg.convert('RGB').resize((838,630),Image.Resampling.LANCZOS).save(ROOT/'docs/previews/default-rifle-scope.png')
print(f'{DEST}: {len(surface)} surface voxels; default model and scope generated')
