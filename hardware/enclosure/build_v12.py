"""Rebuild V12 printable meshes in mm. See PRINTING-V12.md for assembly assumptions."""
from pathlib import Path
import json
import itertools
import numpy as np
import trimesh
from manifold3d import Manifold as M, Mesh

OUT = Path(__file__).parent / 'v12'
OUT.mkdir(exist_ok=True)
panel_recess=3.0
panel_front_gap=0.2
panel_rear_gap=0.2
panel_slot_front=panel_recess-panel_front_gap
panel_slot_back=panel_recess+14.5+panel_rear_gap
panel_slot_depth=panel_slot_back-panel_slot_front
def box(x,y,z,w,d,h): return M.cube((w,d,h)).translate((x,y,z))
def union(items): return M.batch_boolean(items, __import__('manifold3d').OpType.Add)
def rounded(x,y,z,w,d,h,r):
    ball=M.sphere(r,24)
    return M.batch_hull([ball.translate((a,b,c)) for a,b,c in itertools.product((x+r,x+w-r),(y+r,y+d-r),(z+r,z+h-r))])
def hole(x,y,z,d,length): return M.cylinder(length,d/2,circular_segments=48).rotate((-90,0,0)).translate((x,y,z))
def mesh(m):
    a=m.to_mesh()
    return trimesh.Trimesh(np.asarray(a.vert_properties)[:,:3], np.asarray(a.tri_verts), process=True)
def window():
    # Smoothly narrowed square opening with rounded corners, along the depth axis Y.
    vertices=[]
    for t in np.linspace(0,1,33):
        width=72.8+5.2*(1-np.sin(t*np.pi/2)); r=.8+2.6*(1-np.sin(t*np.pi/2))
        for cx,cz,angle in [(1,1,0),(-1,1,90),(-1,-1,180),(1,-1,270)]:
            for a in np.linspace(angle,angle+90,9,endpoint=False):
                a=np.deg2rad(a)
                vertices.append([43.25+cx*(width/2-r)+r*np.cos(a), -.05+(panel_recess-panel_front_gap+.1)*t,43.25+cz*(width/2-r)+r*np.sin(a)])
    n=36; faces=[]
    for j in range(32):
        for i in range(n):
            a=j*n+i;b=j*n+(i+1)%n;c=b+n;d=a+n
            faces.extend([(a,b,c),(a,c,d)])
    for i in range(1,n-1): faces.extend([(0,i+1,i),(32*n,32*n+i,32*n+i+1)])
    tm=trimesh.Trimesh(vertices,faces); tm.fix_normals()
    return M(Mesh(np.asarray(tm.vertices,dtype=np.float32),np.asarray(tm.faces,dtype=np.uint32)))

outer=rounded(-2,0,-3,90.5,90.5,92.5,2)
# Panel slot: 0.2 mm above and below the measured 86.5 mm panel. Rear chamber stays open.
# Overlap the cavity cutters by 0.2 mm to avoid residual coplanar faces.
panel_vertical_gap=0.2
panel_slot_height=86.5+2*panel_vertical_gap
shell=outer-window()-box(-3,panel_slot_front,-panel_vertical_gap,93,panel_slot_depth,panel_slot_height)-box(1,panel_slot_back-.2,-.4,84.5,92.3-(panel_slot_back-.2),87.3)
# Two left end stops leave the speaker area open; overlap the top/bottom slot walls by 0.1 mm.
left_stop=union([box(-2,panel_slot_front-.1,-.3,1.95,panel_slot_depth+.2,4.3),
                 box(-2,panel_slot_front-.1,82.5,1.95,panel_slot_depth+.2,4.3)])
shell+=left_stop
# Rear lips capture only 2 mm of the panel back-shell at top and bottom.
guide_y=panel_slot_back
guide_lips=union([box(-2,guide_y,-.5,90.5,2,2.5),
                  box(-2,guide_y,84.5,90.5,2,2.5)])
# Small lead-in at the right entry, outside the seated panel width.
lead_in=M.hull_points([[x,y,z] for x,y in [(86.5,guide_y-.01),(88.6,guide_y-.01),(88.6,guide_y+.3)] for z in [-1,90]])
guide_lips-=lead_in
shell+=guide_lips
tray=box(7.1,28.2,-.5,72.3,44.9,2)
rail=box(7.1,28.2,1.4,72.3,2,6.1)
for x in (23.5,63.5): rail-=box(x-3.3,28.19,4.2,6.6,2.02,2.4)
tray=union([tray,rail,box(7.1,71.1,1.4,72.3,2,6.1),box(7.1,28.2,1.4,2,44.9,6.1),box(77.4,45.65,1.4,2,12,6.1)])
arms=[]
for x in (23.5,63.5):
    tray+=box(x-5,74,-.5,10,8,22.5)-hole(x,73.9,18,2.5,8.2)
    arms.extend([box(x-3,30.2,22,6,52.3,2.5),box(x-3,30.2,5.2,6,1.5,19.3),box(x-3,27.2,5.2,6,3.1,1.2),box(x-5,82,13,10,3,11.5)])
retainer=union(arms+[box(20.5,30.2,22,46,2,2.5)])
for x in (23.5,63.5): retainer-=hole(x,81.9,18,3.2,3.2)
# Four rear bosses touch both adjacent shell walls, with blind pilot holes.
lid_screws=list(itertools.product((6,80.5),(6,80.5)))
for x,z in lid_screws:
    shell+=box(x-5,80.5,z-5,10,10,10)-hole(x,82.1,z,2.5,8.5)
shell+=tray
lid=rounded(-2,90.5,-3,90.5,4,92.5,1)
# Rear locating lip has 0.3 mm clearance to chamber. Clearance around four screw bosses.
lip=box(1.3,88.5,-.1,83.9,2.1,86.7)-box(3.3,88.4,1.9,79.9,2.3,82.7)
for x,z in lid_screws: lip-=box(x-5.3,88.4,z-5.3,10.6,2.3,10.6)
lid+=lip
for x,z in lid_screws:
    lid-=hole(x,88.4,z,3.2,6.3)
    lid-=hole(x,92.3,z,6.4,2.3)
for z in (47,51,55,59,63):
    lid-=union([box(32.25,90.3,z-.75,22,4.4,1.5),hole(32.25,90.3,z,1.5,4.4),hole(54.25,90.3,z,1.5,4.4)])
parts={'shell':shell,'lid':lid,'retainer':retainer}
report={}
def passage_hits(tm,x,z):
    tri=tm.triangles; direction=np.array([0.,1.,0.]);origin=np.array([x,-10.,z])
    e1=tri[:,1]-tri[:,0];e2=tri[:,2]-tri[:,0];h=np.cross(np.broadcast_to(direction,e2.shape),e2)
    det=(e1*h).sum(axis=1);valid=abs(det)>1e-9;inv=np.zeros_like(det);inv[valid]=1/det[valid]
    s=origin-tri[:,0];u=inv*(s*h).sum(axis=1);q=np.cross(s,e1);v=inv*(q*direction).sum(axis=1);t=inv*(e2*q).sum(axis=1)
    return t[valid&(u>=-1e-6)&(v>=-1e-6)&(u+v<=1.000001)&(t>=0)]-10
for x,z in itertools.product((20,43.25,66),(25,43.25,65)):
    hits=passage_hits(mesh(shell),x,z)
    if len(hits):raise RuntimeError(('blocked passage',x,z,hits.tolist()))
report['version']='V12.9'
report['front_tongues_mm']={'count':2,'free_length':3,'rail_thickness':2,'outer_projection':1,'tongue_width':6,'tongue_height':1.2,'through_slot_width':6.6,'through_slot_height':2.4,'side_clearance_each':0.3,'upper_clearance':0.2,'lower_assembly_relief':1.0}
report['rear_guides_mm']={'count':2,'panel_edge_overlap':2,'depth':2,'rear_gap':panel_rear_gap,'entry_chamfer':0.3}
report['panel_recess_mm']=panel_recess
report['left_stop_mm']={'count':2,'width':1.95,'panel_edge_coverage_each':4,'center_open_height':78.5,'edge_gap':0.05}
report['panel_front_gap_mm']=panel_front_gap
report['panel_rear_gap_mm']=panel_rear_gap
report['panel_slot_depth_mm']=panel_slot_depth
report['panel_vertical_gap_mm']=panel_vertical_gap
report['panel_slot_height_mm']=panel_slot_height
report['front_to_rear_clear_rays']=9
for name,obj in parts.items():
    tm=mesh(obj)
    report[name]={'watertight':bool(tm.is_watertight),'components':len(tm.split()),'bounds_mm':tm.bounds.tolist(),'volume_mm3':float(tm.volume),'triangles':len(tm.faces)}
    if not tm.is_watertight or len(tm.split())!=1 or tm.volume<=0: raise RuntimeError((name,report[name]))
    tm.export(OUT/f'{name}-assembly.stl')
    printable=obj.rotate((90,0,0)) if name=='shell' else obj.rotate((-90,0,0)) if name=='lid' else obj.rotate((180,0,0))
    tm=mesh(printable); tm.apply_translation(-tm.bounds[0]); tm.export(OUT/f'{name}-print.stl')
for a,b in itertools.combinations(parts,2):
    overlap=(parts[a]^parts[b]).volume()
    report[f'{a}/{b}_overlap_mm3']=overlap
    if overlap>0.01: raise RuntimeError(('overlap',a,b,overlap))
references={'panel':box(0,panel_recess,0,86.5,14.5,86.5),'battery':box(9.9,33,1.5,66.7,37.3,20)}
for name,obj in parts.items():
    for ref,shape in references.items():
        overlap=max(0,float((obj^shape).volume()))
        report[f'{name}/{ref}_overlap_mm3']=overlap
        if overlap>0.01: raise RuntimeError(('reference overlap',name,ref,overlap))
# Check directional insertion against the actual shell, not just the seated position.
left_overlap=float((shell^box(-.2,panel_recess,0,86.5,14.5,86.5)).volume())
assert left_overlap>0.1, 'Left stop does not block outward motion'
for shift in (0.2,20,86.5):
    assert (shell^box(shift,panel_recess,0,86.5,14.5,86.5)).volume()<0.01, 'Right insertion collision'
report['left_shift_0_2mm_collision_mm3']=left_overlap
report['right_insertion_checks_passed']=3
rear_shift_overlap=float((guide_lips^box(0,panel_recess+panel_rear_gap+.15,0,86.5,14.5,86.5)).volume())
assert rear_shift_overlap>0.1, 'Rear guide lips do not capture panel back edge'
report['rear_shift_check_mm']=panel_rear_gap+.15
report['rear_shift_guide_collision_mm3']=rear_shift_overlap
(OUT/'mesh-report.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
scene=trimesh.Scene()
for name,obj in parts.items():
    tm=mesh(obj);tm.visual.face_colors={'shell':[85,147,155,255],'lid':[132,114,159,255],'retainer':[238,167,83,255]}[name]
    scene.add_geometry(tm,node_name=name)
scene.export(OUT/'assembly.glb')
# Small samples let the printer tune clearances before committing to the full shell.
samples={
 'fit-panel-guide':(shell^box(82,-.1,-3.1,6.6,20.1,92.7)).rotate((90,0,0)),
 'fit-panel-slot':(box(-5,panel_slot_front,-3,15,panel_slot_depth,92.5)-box(-2,panel_slot_front-.1,-panel_vertical_gap,13,panel_slot_depth+.2,panel_slot_height)).rotate((90,0,0)),
 'fit-rail':shell^box(18,27,-.5,11,7,8.5),
 'fit-tongue':retainer^box(20,26.9,5,7,6.1,20),
 'fit-screws':union([box(0,0,0,45,15,2)]+[M.cylinder(10,5,circular_segments=48).translate((x,7.5,1.9))-M.cylinder(9,d/2,circular_segments=48).translate((x,7.5,4)) for x,d in [(7.5,2.4),(22.5,2.5),(37.5,2.6)]])
}
for name,obj in samples.items():
    if name=='fit-tongue':obj=obj.rotate((180,0,0))
    tm=mesh(obj)
    assert tm.is_watertight and len(tm.split())==1
    tm.apply_translation(-tm.bounds[0]);tm.export(OUT/f'{name}.stl')
print(json.dumps(report,indent=2))
import subprocess
import sys
subprocess.run([sys.executable,str(Path(__file__).parent/'check_screen_opening.py')],check=True)
subprocess.run([sys.executable,str(Path(__file__).parent/'check_retainer_removal.py')],check=True)
