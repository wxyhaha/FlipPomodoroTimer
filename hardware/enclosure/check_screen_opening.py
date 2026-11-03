"""Check the official 71.93mm active square against the actual exported shell STL."""
from pathlib import Path
import hashlib
import itertools
import json
import math
import numpy as np
import trimesh
from manifold3d import Manifold as M, Mesh

ROOT=Path(__file__).parent/'v12'
path=ROOT/'shell-assembly.stl'
settings=json.loads((ROOT/'mesh-report.json').read_text(encoding='utf-8'))
tm=trimesh.load_mesh(path)
shell=M(Mesh(np.asarray(tm.vertices,dtype=np.float32),np.asarray(tm.faces,dtype=np.uint32)))
active=71.93;center=43.25;recess=settings['panel_recess_mm'];rows=[]
vertical_gap=settings['panel_vertical_gap_mm']
for dx,dz in itertools.product((-.05,0,.05),(-vertical_gap,0,vertical_gap)):
    for axis,angle in [('x',0),('x',-15),('x',15),('x',-30),('x',30),('z',-15),('z',15),('z',-30),('z',30)]:
        slope=math.tan(math.radians(angle));points=[]
        for x,y,z in itertools.product((center-active/2+dx,center+active/2+dx),(-.1,recess),(center-active/2+dz,center+active/2+dz)):
            points.append([x+(recess-y)*slope*(axis=='x'),y,z+(recess-y)*slope*(axis=='z')])
        overlap=max(0,float((shell^M.hull_points(points)).volume()))
        rows.append(dict(offset_x_mm=dx,offset_z_mm=dz,axis=axis,angle_deg=angle,blocked_volume_mm3=overlap))
report={
 'version':settings['version'],
 'official_dimension_image':'https://docs.waveshare.net/assets/images/ESP32-S3-Touch-LCD-4B-details-size-10826e511c001d8541b2a98abc4337d6.webp',
 'active_area_mm':[active,active], 'panel_size_mm':[86.5,86.5],
 'assumptions':['Active square centered in panel; official image does not dimension its offset.','Active plane at panel front; actual glass optical stack not modeled.','Horizontal offset +/-0.05 mm is a checked scenario, not guaranteed right-side retention; vertical offset follows panel vertical clearance.'],
 'minimum_aperture_width_mm':72.8,'straight_edge_nominal_margin_mm':(72.8-active)/2,
 'shell_stl_sha256':hashlib.sha256(path.read_bytes()).hexdigest(),
 'checks':rows,'passed':all(r['blocked_volume_mm3']<1e-5 for r in rows)
}
report['normal_view_passed']=all(r['blocked_volume_mm3']<1e-5 for r in rows if r['angle_deg']==0)
report['oblique_blocked_cases']=sum(r['blocked_volume_mm3']>=1e-5 for r in rows if r['angle_deg']!=0)
(ROOT/'screen-check.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
if not report['normal_view_passed']:raise RuntimeError('Active area occluded in normal view: see screen-check.json')
print(f"Screen check {settings['version']}: normal view passed; {report['oblique_blocked_cases']} oblique cases have occlusion among {len(rows)} total. See report assumptions.")
