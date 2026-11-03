"""Sample a removal route with panel/lid removed and a rounded battery reference."""
from pathlib import Path
import json
import numpy as np
import trimesh
from manifold3d import Manifold as M, Mesh

HERE=Path(__file__).parent
ROOT=HERE/'v12'
def load(name):
    tm=trimesh.load_mesh(ROOT/f'{name}-assembly.stl')
    return M(Mesh(np.asarray(tm.vertices,dtype=np.float32),np.asarray(tm.faces,dtype=np.uint32)))
shell=load('shell');retainer=load('retainer')
battery=M.batch_hull([M.cylinder(66.7,10,circular_segments=64).rotate((0,90,0)).translate((9.9,y,11.5)) for y in (43,60.3)])
obstacle=shell+battery
route=json.loads((HERE/'retainer-removal-path.json').read_text(encoding='utf-8'))
pivot=np.asarray(route['pivot_xyz_mm']);samples=[]
poses=[np.array([p['backward_mm'],p['angle_x_deg'],p['vertical_mm']]) for p in route['poses']]
for a,b in zip(poses,poses[1:]):
    samples.extend(a+(b-a)*t for t in np.linspace(0,1,9,endpoint=False))
samples.append(poses[-1])
for lift in np.linspace(.25,20,80):samples.append(poses[-1]+[0,0,lift])
for back in np.linspace(1,90,90):samples.append(poses[-1]+[back,0,20])
maximum=0.0
for i,(dy,angle,dz) in enumerate(samples):
    p=retainer.translate(-pivot).rotate((float(angle),0,0)).translate(pivot+[0,dy,dz])
    overlap=max(0,float((p^obstacle).volume()))
    maximum=max(maximum,overlap)
    if overlap>.0005:raise RuntimeError(('removal interference',i,dy,angle,dz,overlap))
report={'version':json.loads((ROOT/'mesh-report.json').read_text())['version'],
        'assumptions':route['assumptions'],'sampled_poses':len(samples),'max_overlap_mm3':maximum,
        'status':'sampled geometric route clear; not physical assembly validation',
        'sequence':'Remove lid and panel, loosen two retainer screws, gradually tip and retreat retainer; lift and withdraw through rear opening. Reverse order to install battery retainer before panel.'}
(ROOT/'retainer-removal-check.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
print('Retainer removal route:',len(samples),'poses, max overlap',maximum,'mm3; assumptions apply.')
