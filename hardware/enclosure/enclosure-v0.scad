// FlipPomodoroTimer enclosure concept V0, units mm.
// NOT a final printable fit. Panel includes its existing factory housing.
panel_w = 86.5;
panel_h = 86.5;
panel_d = 14.5;
slide_clearance = 0.4;
wall = 2;
body_depth = 90.5;
right_guard = 2;
show_panel = true;
show_battery = true;
show_lid = true;
explode_lid = 0;

// X left/right facing screen; Y front->rear; Z bottom->top.
module part(x,y,z,w,d,h) { translate([x,y,z]) cube([w,d,h]); }
module shell() {
  slot_back = panel_d + slide_clearance;
  // Front slot goes completely through X: no left/right front sidewalls.
  part(0,0,-wall,panel_w,body_depth,wall);
  part(0,0,panel_h,panel_w,body_depth,wall);
  part(0,slot_back,0,wall,body_depth-slot_back,panel_h);
  part(panel_w-wall,slot_back,0,wall,body_depth-slot_back,panel_h);
  // Front lips touch only bezel; rear shoulders locate panel in depth.
  part(0,-wall,0,panel_w,wall,1.8);
  part(0,-wall,panel_h-1.8,panel_w,wall,1.8);
  part(0,slot_back,0,panel_w,wall,2);
  part(0,slot_back,panel_h-2,panel_w,wall,2);
  // Two outboard feet protect buttons without covering buttons/USB.
  part(panel_w,0,-wall,right_guard,slot_back+wall,wall);
  part(panel_w,0,panel_h,right_guard,slot_back+wall,wall);
}
module rear_lid() {
  // Screw attachment, speaker openings and wire routing remain unfinalized.
  part(0,body_depth+explode_lid,-wall,panel_w,wall,panel_h+2*wall);
}
module panel_reference() { part(0,0,0,panel_w,panel_d,panel_h); }
module battery_reference() {
  // Earlier listing envelope only, replace with measured pack when delivered.
  translate([panel_w/2-9,48,10]) cylinder(h=66.5,r=9.2,$fn=48);
  translate([panel_w/2+9,48,10]) cylinder(h=66.5,r=9.2,$fn=48);
}
color("ivory") shell();
if (show_lid) color("ivory") rear_lid();
if (show_panel) color([.08,.10,.12]) panel_reference();
if (show_battery) color([.12,.38,.78]) battery_reference();
