// V11: one low-profile U retainer, inner-side front tongues + two rear-facing screws.
// Trial-fit concept only; fastener and battery clearances still need a printed sample.
part_to_show="assembly"; // assembly, shell, u_frame, screws, lid
panel_w=86.5; panel_h=86.5; panel_d=14.5;
recess=6; wall=3; clearance=0.4; body_depth=90.5; side_guard=2;
body_x=-side_guard; body_w=panel_w+2*side_guard;
front_opening=78; throat_opening=72.8;
show_panel=true; show_bezel=true; show_lid=false; show_battery=true;
show_retainer=true; show_screws=true;
battery_l=66.7; battery_w=37.3; battery_h=20;
battery_x=(panel_w-battery_l)/2; battery_y=33; battery_z=1.5;
tray_gap=0.8; tray_front_extension=2;
strap_centers=[23.5,63.5]; strap_w=6; strap_h=2.5;
strap_z=battery_z+battery_h+0.5; // 0.5mm clearance above the measured pack.
front_rail_y=28.2; rear_rail_y=71.1;
latch_depth=0.8; latch_gap=0.1; latch_slot_z=5.0; latch_slot_h=1.6;
rear_boss_y=74; rear_boss_d=8; rear_boss_h=strap_z;
ear_y=82; ear_d=3; screw_z=18;
screw_clearance=3.2; pilot_d=2.5; screw_length=10; // Candidate M3 self-tapping screws into printed bosses.
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
module rear_anchor(cx) {
  // Integral post is behind the battery. Its pilot hole opens toward the rear service opening.
  difference() {
    part(cx-5,rear_boss_y,0,10,rear_boss_d,rear_boss_h);
    translate([cx,rear_boss_y-0.01,screw_z]) rotate([-90,0,0]) cylinder(h=rear_boss_d+0.02,d=pilot_d);
  }
}
module battery_tray() {
  x=battery_x-tray_gap-2; y=battery_y-tray_gap-2-tray_front_extension;
  l=battery_l+2*tray_gap+4; w=battery_w+2*tray_gap+4+tray_front_extension;
  part(x,y,0,l,w,battery_z);
  // Blind pockets open only toward the battery bay; each retains a 1.1mm outer wall.
  difference() {
    part(x,y,battery_z,l,2,6);
    for(cx=strap_centers)
      part(cx-(strap_w+0.6)/2,front_rail_y+2-latch_depth-latch_gap,latch_slot_z,
           strap_w+0.6,latch_depth+latch_gap+0.01,latch_slot_h);
  }
  part(x,rear_rail_y,battery_z,l,2,6);
  part(x,y,battery_z,2,w,6);
  // Low right end stop remains open around the short battery lead.
  part(battery_x+battery_l+tray_gap,battery_y+battery_w/2-6,battery_z,2,12,6);
  for(cx=strap_centers) rear_anchor(cx);
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
module front_tongue(cx) {
  // The spring fingers sit inside the battery bay; short toes enter the inner-facing blind pockets.
  part(cx-strap_w/2,front_rail_y+2,5.2,strap_w,1.5,strap_z+strap_h-5.2);
  part(cx-strap_w/2,front_rail_y+2-latch_depth,5.2,strap_w,latch_depth,1.2);
}
module u_frame() {
  difference() {
    union() {
      // Two narrow arms bridge the pack lengthwise; the front crosspiece joins them as one part.
      for(cx=strap_centers) {
        part(cx-strap_w/2,front_rail_y+2,strap_z,strap_w,ear_y-(front_rail_y+2)+0.5,strap_h);
        front_tongue(cx);
        // Rear ears sit against the integral bosses; screw heads remain accessible from the back.
        part(cx-5,ear_y,13,10,ear_d,13);
      }
      part(strap_centers[0]-strap_w/2,front_rail_y+2,strap_z,
           strap_centers[1]-strap_centers[0]+strap_w,2,strap_h);
    }
    for(cx=strap_centers)
      translate([cx,ear_y-0.01,screw_z]) rotate([-90,0,0]) cylinder(h=ear_d+0.02,d=screw_clearance);
  }
}
module screw_reference(cx) {
  // Visual-only M3 fastener; exclude from printable parts.
  color([0.72,0.75,0.79]) {
    translate([cx,ear_y+ear_d,screw_z]) rotate([90,0,0]) cylinder(h=screw_length,d=3);
    translate([cx,ear_y+ear_d+1,screw_z]) rotate([90,0,0]) cylinder(h=1,d=5.5);
  }
}
module battery_reference() {
  hull() for(dy=[battery_h/2,battery_w-battery_h/2])
    translate([battery_x,battery_y+dy,battery_z+battery_h/2])
      rotate([0,90,0]) cylinder(h=battery_l,r=battery_h/2);
}
module screws() for(cx=strap_centers) screw_reference(cx);

if(part_to_show=="u_frame") u_frame();
if(part_to_show=="screws") screws();
if(part_to_show=="shell") union() { housing(); bezel(); }
if(part_to_show=="lid") part(body_x,body_depth,-wall,body_w,wall,panel_h+2*wall);
if(part_to_show=="assembly") {
  color([0.46,0.55,0.62]) housing();
  if(show_bezel) color([0.12,0.67,0.64]) bezel();
  if(show_lid) color([0.54,0.45,0.67]) part(body_x,body_depth,-wall,body_w,wall,panel_h+2*wall);
  if(show_retainer) color([0.95,0.62,0.29]) u_frame();
  if(show_screws) screws();
  if(show_panel) {
    color([0.15,0.18,0.23]) part(0,recess,0,panel_w,panel_d,panel_h);
    color([0.08,0.16,0.22]) part(7.285,recess-0.01,7.285,71.93,0.01,71.93);
  }
  if(show_battery) color([0.20,0.48,0.84]) battery_reference();
}
