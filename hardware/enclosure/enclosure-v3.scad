// V3 symmetric integrated guard edges, +/-2mm beyond panel. Not print-ready.
panel_w=86.5; panel_h=86.5; panel_d=14.5;
recess=6; // Provisional reveal depth; independent of structural wall thickness.
wall=3; clearance=0.4; body_depth=90.5; side_guard=2;
body_x=-side_guard;
body_w=panel_w+2*side_guard;
front_opening=78; throat_opening=72.8; // Active area 71.93, leave alignment margin.
show_panel=true; show_bezel=true; show_lid=true; show_battery=true;
slot_back=recess+panel_d+clearance;
$fn=48;
module part(x,y,z,w,d,h) { translate([x,y,z]) cube([w,d,h]); }
function opening(t)=throat_opening+(front_opening-throat_opening)*(1-sin(t*90));
function corner(t)=0.8+2.6*(1-sin(t*90));
module aperture(t,y) {
  translate([panel_w/2,y,panel_h/2]) rotate([-90,0,0])
    linear_extrude(height=0.015)
      offset(r=corner(t)) square([opening(t)-2*corner(t),opening(t)-2*corner(t)],center=true);
}
module bezel() {
  difference() {
    part(body_x,0,-wall,body_w,recess,panel_h+2*wall);
    // Curved mouth, piecewise lofted through rounded-square sections.
    for(i=[0:23]) hull() {
      aperture(i/24,recess*i/24-0.01);
      aperture((i+1)/24,recess*(i+1)/24+0.01);
    }
  }
}
module housing() {
  // Full-width upper/lower skins cover the factory panel along its entire depth.
  // Symmetric 2mm overhangs are integral to these skins, not corner/foot pieces.
  part(body_x,recess,-wall,body_w,body_depth-recess,wall);
  part(body_x,recess,panel_h,body_w,body_depth-recess,wall);
  // Lateral slot remains open only across the panel's depth.
  part(body_x,slot_back,0,wall,body_depth-slot_back,panel_h);
  part(body_x+body_w-wall,slot_back,0,wall,body_depth-slot_back,panel_h);
  part(0,slot_back,0,panel_w,wall,2);
  part(0,slot_back,panel_h-2,panel_w,wall,2);
}
color([0.46,0.55,0.62]) housing();
if(show_bezel) color([0.12,0.67,0.64]) bezel();
if(show_lid) color([0.54,0.45,0.67]) part(body_x,body_depth,-wall,body_w,wall,panel_h+2*wall);
if(show_panel) {
  color([0.15,0.18,0.23]) part(0,recess,0,panel_w,panel_d,panel_h);
  color([0.08,0.16,0.22]) part(7.285,recess-0.01,7.285,71.93,0.01,71.93);
}
if(show_battery) color([0.20,0.48,0.84])
  for(x=[panel_w/2-9,panel_w/2+9]) translate([x,48,10]) cylinder(h=66.5,r=9.2);
