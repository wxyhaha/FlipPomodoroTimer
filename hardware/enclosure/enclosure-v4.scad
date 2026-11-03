// V4 measured battery flat in lower bay. Not print-ready.
panel_w=86.5; panel_h=86.5; panel_d=14.5;
recess=6; // Provisional reveal depth; independent of structural wall thickness.
wall=3; clearance=0.4; body_depth=90.5; side_guard=2;
body_x=-side_guard;
body_w=panel_w+2*side_guard;
front_opening=78; throat_opening=72.8; // Active area 71.93, leave alignment margin.
show_panel=true; show_bezel=true; show_lid=true; show_battery=true;
battery_l=66.7; battery_w=37.3; battery_h=20; // User measured wrapped pack.
battery_x=(panel_w-battery_l)/2; battery_y=33; battery_z=1.5;
tray_gap=0.8; // Each lateral side, provisional fit, not measured print allowance.
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
  battery_tray();
}
module battery_tray() {
  // Shallow locating tray, right end open for removal and provisional wire exit.
  x=battery_x-tray_gap-2; y=battery_y-tray_gap-2;
  l=battery_l+2*tray_gap+4; w=battery_w+2*tray_gap+4;
  part(x,y,0,l,w,battery_z);
  for(rail_y=[y,y+w-2]) part(x,rail_y,battery_z,l,2,6);
  part(x,y,battery_z,2,w,6);
  // No retaining strap/clamp or finalized cable notch yet.
}
module battery_reference() {
  // Rounded envelope of wrapped 1S2P pack; long axis runs left/right.
  hull() for(dy=[battery_h/2,battery_w-battery_h/2])
    translate([battery_x,battery_y+dy,battery_z+battery_h/2])
      rotate([0,90,0]) cylinder(h=battery_l,r=battery_h/2);
}
color([0.46,0.55,0.62]) housing();
if(show_bezel) color([0.12,0.67,0.64]) bezel();
if(show_lid) color([0.54,0.45,0.67]) part(body_x,body_depth,-wall,body_w,wall,panel_h+2*wall);
if(show_panel) {
  color([0.15,0.18,0.23]) part(0,recess,0,panel_w,panel_d,panel_h);
  color([0.08,0.16,0.22]) part(7.285,recess-0.01,7.285,71.93,0.01,71.93);
}
if(show_battery) color([0.20,0.48,0.84]) battery_reference();
