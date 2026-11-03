// V5 removable battery bars with positive-height stops. Trial-fit design.
part_to_show="assembly"; // assembly, shell, strap, lid
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
bar_centers=[23.5,63.5]; bar_w=8; bar_thick=3;
pad_space=1.5; bar_z=battery_z+battery_h+pad_space; // 23mm stop height.
front_boss_y=22.2; rear_boss_y=71.1; boss_size=10;
screw_clearance=3.4; nut_af=5.8; nut_z=16.8; nut_thick=2.8;
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
  for(x=bar_centers) for(y=[front_boss_y,rear_boss_y]) clamp_boss(x,y);
}
module battery_tray() {
  // Shallow locating tray, right end open for removal and provisional wire exit.
  x=battery_x-tray_gap-2; y=battery_y-tray_gap-2;
  l=battery_l+2*tray_gap+4; w=battery_w+2*tray_gap+4;
  part(x,y,0,l,w,battery_z);
  for(rail_y=[y,y+w-2]) part(x,rail_y,battery_z,l,2,6);
  part(x,y,battery_z,2,w,6);
  // Low right end stop; surrounding open area leaves room for the short wire.
  part(battery_x+battery_l+tray_gap,battery_y+battery_w/2-6,battery_z,2,12,6);
}
module clamp_boss(cx,y) {
  difference() {
    part(cx-boss_size/2,y,0,boss_size,boss_size,bar_z);
    translate([cx,y+boss_size/2,15]) cylinder(h=bar_z-15+0.1,d=screw_clearance);
    translate([cx,y+boss_size/2,nut_z]) cylinder(h=nut_thick,r=nut_af/sqrt(3),$fn=6);
    // Side-load an ordinary M3 nut: no heat-set insert required.
    part(cx-boss_size/2-0.1,y+boss_size/2-nut_af/2,nut_z,boss_size/2+0.1,nut_af,nut_thick);
  }
}
module retaining_bar() {
  difference() {
    cube([bar_w,rear_boss_y+boss_size-front_boss_y,bar_thick]);
    for(y=[boss_size/2,rear_boss_y-front_boss_y+boss_size/2])
      translate([bar_w/2,y,-0.1]) cylinder(h=bar_thick+0.2,d=screw_clearance);
  }
}
module battery_reference() {
  // Rounded envelope of wrapped 1S2P pack; long axis runs left/right.
  hull() for(dy=[battery_h/2,battery_w-battery_h/2])
    translate([battery_x,battery_y+dy,battery_z+battery_h/2])
      rotate([0,90,0]) cylinder(h=battery_l,r=battery_h/2);
}
if(part_to_show=="strap") retaining_bar();
if(part_to_show=="shell") union() { housing(); bezel(); }
if(part_to_show=="lid") part(body_x,body_depth,-wall,body_w,wall,panel_h+2*wall);
if(part_to_show=="assembly") {
color([0.46,0.55,0.62]) housing();
if(show_bezel) color([0.12,0.67,0.64]) bezel();
if(show_lid) color([0.54,0.45,0.67]) part(body_x,body_depth,-wall,body_w,wall,panel_h+2*wall);
for(x=bar_centers) {
  color([0.95,0.68,0.32]) translate([x-bar_w/2,front_boss_y,bar_z]) retaining_bar();
  // Foam reference only: not a rigid printable solid.
  color([0.34,0.70,0.52]) part(x-bar_w/2,battery_y,bar_z-pad_space,bar_w,battery_w,pad_space);
}
if(show_panel) {
  color([0.15,0.18,0.23]) part(0,recess,0,panel_w,panel_d,panel_h);
  color([0.08,0.16,0.22]) part(7.285,recess-0.01,7.285,71.93,0.01,71.93);
}
if(show_battery) color([0.20,0.48,0.84]) battery_reference();
}
