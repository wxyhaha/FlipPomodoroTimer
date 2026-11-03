"""Render the exported meshes; illustration only, not a slicer simulation."""
from pathlib import Path
import zipfile
import shutil
import numpy as np
import trimesh
from PIL import Image, ImageDraw, ImageFont

ROOT=Path(__file__).parent/'v12'
S=2
im=Image.new('RGB',(1600*S,900*S),(239,243,246)); draw=ImageDraw.Draw(im)
font=lambda size:ImageFont.truetype('C:/Windows/Fonts/msyh.ttc',size*S)
def label(x,y,text,size=20): draw.text((x*S,y*S),text,font=font(size),fill=(28,43,53))
def box_mesh(bounds):
    lo,hi=np.array(bounds);m=trimesh.creation.box(hi-lo);m.apply_translation((lo+hi)/2);return m
parts={n:trimesh.load_mesh(ROOT/f'{n}-assembly.stl') for n in ['shell','lid','retainer']}
panel=box_mesh([[0,3,0],[86.5,17.5,86.5]])
screen=box_mesh([[7.285,2.98,7.285],[79.215,3,79.215]])
battery=box_mesh([[9.9,33,1.5],[76.6,70.3,21.5]])
def render(cx,cy,eye,cut=False):
    eye=np.array(eye,dtype=float);eye/=np.linalg.norm(eye)
    right=np.cross([0,0,1],eye);right/=np.linalg.norm(right);up=np.cross(eye,right)
    rows=[]; pixels=np.array(im); depth_buffer=np.full((im.height,im.width),-np.inf)
    items=[(parts['shell'],(80,150,157)),(panel,(60,68,78)),(screen,(22,39,48)),(battery,(68,127,211)),(parts['retainer'],(236,167,82))]
    lid=parts['lid'].copy();lid.apply_translation([0,24 if cut else 0,0]);items.append((lid,(146,127,168)))
    for tm,color in items:
        if cut and tm is parts['shell']:
            tm=trimesh.boolean.intersection([tm,box_mesh([[-3,-1,-4],[43.25,94,91]])],engine='manifold')
        p=(tm.vertices-[43.25,45,43.25]); xy=np.stack([p@right,-p@up],axis=1)*3.8*S+[cx*S,cy*S]
        depths=p@eye
        for face,normal in zip(tm.faces,tm.face_normals):
            light=.65+.28*abs(normal@np.array([.3,-.4,.866]))
            rows.append((depths[face],xy[face],tuple(int(v*light) for v in color)))
    for depth,xy,color in rows:
        x0,y0=np.maximum(np.floor(xy.min(axis=0)).astype(int),0)
        x1,y1=np.minimum(np.ceil(xy.max(axis=0)).astype(int),[im.width-1,im.height-1])
        if x1<x0 or y1<y0: continue
        a,b,c=xy;den=(b[1]-c[1])*(a[0]-c[0])+(c[0]-b[0])*(a[1]-c[1])
        if abs(den)<1e-8: continue
        yy,xx=np.mgrid[y0:y1+1,x0:x1+1];xx=xx+.5;yy=yy+.5
        u=((b[1]-c[1])*(xx-c[0])+(c[0]-b[0])*(yy-c[1]))/den
        v=((c[1]-a[1])*(xx-c[0])+(a[0]-c[0])*(yy-c[1]))/den
        w=1-u-v;z=u*depth[0]+v*depth[1]+w*depth[2]
        old=depth_buffer[y0:y1+1,x0:x1+1];mask=(u>=-1e-6)&(v>=-1e-6)&(w>=-1e-6)&(z>old)
        old[mask]=z[mask];pixels[y0:y1+1,x0:x1+1][mask]=color
    im.paste(Image.fromarray(pixels))
label(65,30,'V12.9 · 圆润一体外壳 / 分件打印方案',30)
label(65,84,'总包络 90.5 × 94.5 × 92.5 mm  ·  外缘 R2 mm  ·  三个打印件',21)
render(410,425,[1,-1,.75])
render(1190,425,[1,1,.75],True)
label(120,755,'装配外观：内缩3mm，左上、左下止挡，面板从右侧插入',19)
label(875,755,'剖开右半壳并后移后盖：检查电池压条和后盖定位边',19)
label(65,825,'网格已封闭与检查；打印配合、卡舌弹性和翻转牢固性仍需首件试装。',19)
im.resize((1600,900),Image.Resampling.LANCZOS).save(ROOT/'preview.png')
RELEASE=ROOT.parent/'v12.9'
RELEASE.mkdir(exist_ok=True)
for name in ['shell','lid','retainer']:
    shutil.copy2(ROOT/f'{name}-print.stl',RELEASE/f'V12.9-{name}.stl')
for name in ['mesh-report.json','screen-check.json','retainer-removal-check.json','scope-check.json','preview.png','assembly.glb']:
    shutil.copy2(ROOT/name,RELEASE/name)
for name in ['PRINTING-V12.md','SCREEN-OPENING-CHECK.md','FINAL-DIMENSIONS-V12.9.md','screen-opening-template.svg']:
    shutil.copy2(ROOT.parent/name,RELEASE/name)
with zipfile.ZipFile(RELEASE/'print-package-v12-9.zip','w',zipfile.ZIP_DEFLATED) as archive:
    for path in RELEASE.iterdir():
        if path.suffix!='.zip':archive.write(path,path.name)
    for name in ['fit-panel-slot.stl','fit-panel-guide.stl','fit-rail.stl','fit-tongue.stl','fit-screws.stl']:
        archive.write(ROOT/name,'optional-samples/'+name)
shutil.copy2(RELEASE/'print-package-v12-9.zip',ROOT/'print-package-v12-9.zip')
