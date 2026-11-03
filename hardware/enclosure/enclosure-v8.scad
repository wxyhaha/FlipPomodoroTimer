// V8: one small snap-in retaining ring for the measured battery pack.
part_to_show="assembly"; // assembly, shell, snap_clip, lid
panel_w=86.5; panel_h=86.5; panel_d=14.5;
recess=6; wall=3; clearance=0.4; body_depth=90.5; side_guard=2;
body_x=-side_guard; body_w=panel_w+2*side_guard;
front_opening=78; throat_opening=72.8;
show_panel=true; show_bezel=true; show_lid=true; show_battery=true; show_clip=true;
battery_l=66.7; battery_w=37.3; battery_h=20;
battery_x=(panel_w-battery_l)/2; battery_y=33; battery_z=1.5;
tray_gap=0.8;
clip_w=12; clip_x=(panel_w-clip_w)/2;
clip_clearance=0.5; // Gap above the pack before the ring catches it.
clip_under=battery_z+battery_h+clip_clearance; // 22mm
clip_top=clip_under+1.6;
slot_x=clip_x-0.35; slot_w=clip_w+0.7;
slot_z0=5.2; slot_z1=6.4; // Side-entry snap pockets below the rail tops.
slot_front_y=30.2; slot_back_y=71.1;
slot_depth=2.0;
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
    for(i=[0:23]) hull() {
      aperture(i/24,recess*i/24-0.01);
      aperture((i+1)/24,recess*(i+1)/24+0.01);
    }
  }
}
module snap_rail(y) {
  // A small pocket under the rail top lets the flexible ring feet catch the lip.
  difference() {
    part(7.1,y,1.5,72.3,2,6);
    part(slot_x,y-0.01,slot_z0,slot_w,2.02,slot_z1-slot_z0);
  }
}
module battery_tray() {
  x=battery_x-tray_gap-2; y=battery_y-tray_gap-2;
  l=battery_l+2*tray_gap+4; w=battery_w+2*tray_gap+4;
  part(x,y,0,l,w,battery_z);
  snap_rail(30.2);
  snap_rail(71.1);
  part(x,y,battery_z,2,w,6);
  // Low right end stop remains open around the short battery lead.
  part(battery_x+battery_l+tray_gap,battery_y+battery_w/2-6,battery_z,2,12,6);
}
module housing() {
  part(body_x,recess,-wall,body_w,body_depth-recess,wall);
  part(body_x,recess,panel_h,body_w,body_depth-recess,wall);
  part(body_x,slot_back,0,wall,body_depth-slot_back,panel_h);
  part(body_x+body_w-wall,slot_back,0,wall,body_depth-slot_back,panel_h);
  part(0,slot_back,0,panel_w,wall,2);
  part(0,slot_back,panel_h-2,panel_w,wall,2);
  battery_tray();
}
module snap_clip() {
  // One narrow spring ring crosses the battery's short dimension.
  // The sloped shoulders form a small lead-in; end feet catch under the tray-rail lips.
  points=[
    [28.8,5.2],[28.8,20.2],[31.2,23.6],[72.1,23.6],[74.5,20.2],[74.5,5.2],
    [71.1,5.2],[73.1,6.4],[73.1,7.5],[70.4,20.4],[69.0,22.0],
    [34.2,22.0],[32.7,20.4],[30.0,7.5],[30.0,6.4],[32.2,5.2]
  ];
  translate([clip_x,0,0]) rotate([90,0,90]) linear_extrude(height=clip_w) polygon(points=points);
}
module battery_reference() {
  hull() for(dy=[battery_h/2,battery_w-battery_h/2])
    translate([battery_x,battery_y+dy,battery_z+battery_h/2])
      rotate([0,90,0]) cylinder(h=battery_l,r=battery_h/2);
}
if(part_to_show=="snap_clip") snap_clip();
if(part_to_show=="shell") union() { housing(); bezel(); }
if(part_to_show=="lid") part(body_x,body_depth,-wall,body_w,wall,panel_h+2*wall);
if(part_to_show=="assembly") {
  color([0.46,0.55,0.62]) housing();
  if(show_bezel) color([0.12,0.67,0.64]) bezel();
  if(show_lid) color([0.54,0.45,0.67]) part(body_x,body_depth,-wall,body_w,wall,panel_h+2*wall);
  if(show_clip) color([0.95,0.58,0.23]) snap_clip();
  if(show_panel) {
    color([0.15,0.18,0.23]) part(0,recess,0,panel_w,panel_d,panel_h);
    color([0.08,0.16,0.22]) part(7.285,recess-0.01,7.285,71.93,0.01,71.93);
  }
  if(show_battery) color([0.20,0.48,0.84]) battery_reference();
}
