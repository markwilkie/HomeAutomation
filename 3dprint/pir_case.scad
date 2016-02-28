// 2 piece case for PIR sensor SR501
// PCB volume size 33x25x14
// created by jegb 2014
// CC BY-NC-SA-3.0

wall_size = 3;
w = 25;
d = 33;
h = 8; //block material size, my stock is 8mm PE or Acrylic
pcb_h = 14/2; //this is the volume inside of each half
// assumes a wall on top/bottom of 1mm

gap= 3;
rad = 3; // soften edges

//show assembled
assy = false;
ears = true;
has_slot=true;

dxf=false;

module lip(l_h,l_w){
// to be differenced from the main body
	difference(){
		cube([w+2*l_w,d+2*l_w,l_h], center=false);
		translate([l_w,l_w,0]) cube([w,d,l_h], center=false);
	}

} //lip(1.5, 1.5);

module createSubtractor(h,radius) {
	difference(){ 
	cube([radius+0.1,radius+0.1,h]); 
	cylinder(h=h,r=radius+0.1,$fn = 25);
	} //+0.1 stops ghost edges
}

module slot(slot_l){ 
	hull(){ //slot
		translate([rad,0,0]) cylinder(h=h, r=rad/2, $fn=25);
		translate([slot_l-rad,0,0]) cylinder(h=h, r=rad/2, $fn=25);
	} // hull inside
} //slot(3, 15);

module ears(sep, ear_w, ear_l){ //sep is distance between ears
   hull(){
	hull(){
		cylinder(h=h/2, r=ear_w/2, $fn=25);
		translate([ear_l,0,0]) cylinder(h=h/2, r=ear_w/2, $fn=25);
	} // hull1

	translate([0,wall_size,0]) hull(){
		translate([-(w-ear_l-2*wall_size),0,0]) cylinder(h=h/2, r=rad/2+wall_size/2, $fn=25);
		translate([w-2*wall_size,0,0]) cylinder(h=h/2, r=rad/2+wall_size/2, $fn=25);
	} // hull2

	translate([0,d+wall_size,0]) hull(){
		translate([-(w-ear_l-2*wall_size),0,0]) cylinder(h=h/2, r=rad/2+wall_size/2, $fn=25);
		translate([w-2*wall_size,0,0]) cylinder(h=h/2, r=rad/2+wall_size/2, $fn=25);
	} // hull3

	translate([0,d+2*wall_size,0]) hull(){
		cylinder(h=h/2, r=ear_w/2, $fn=25);
		translate([ear_l,0,0]) cylinder(h=h/2, r=ear_w/2, $fn=25);
	} // hull4

	}
} //ears(d, 7, 15);


// bottom B side
module case_b(){

	difference(){
		union(){
		cube ([w+2*wall_size,d+2*wall_size,h], center =false);
		if(ears==true) translate([wall_size+w/2-15/2,0]) ears(d,9,15);
		}
		translate ([wall_size,wall_size,h-pcb_h]) cube ([w,d,pcb_h+0.1], center =false);

// rounding corners
		translate([w+2*wall_size-rad,d+2*wall_size-rad]) rotate(0) createSubtractor(h,rad); 
		translate([rad,d+2*wall_size-rad]) rotate(90) createSubtractor(h,rad);
		translate([rad,rad]) rotate(180) createSubtractor(h,rad);
		translate([w+2*wall_size-rad,rad]) rotate(270) createSubtractor(h,rad);

// pass-through dupont plug 8x3x6
		translate([wall_size+3/2,(2*wall_size+d)/2,0]) cube ([3,8,6], center =true); 

// 4xscrews 1.6mm dia (to do)
//		translate([wall_size/2+0.4,wall_size/2+0.4,h/2]) cylinder(h=h, r=0.8, $fn=25);

	if(has_slot==true) {
		translate([(w+2*wall_size)/2-10/2,-((9-wall_size)/2-1),0]) slot(10);
		translate([(w+2*wall_size)/2-10/2,d+2*wall_size+((9-wall_size)/2-1),0]) slot(10);
	}

//1.5mm lip
	translate([wall_size-1.5, wall_size-1.5,h-1.5]) lip(1.5,1.5);

	}// difference

} //color ("orange") case_b();


// top side
module case_a(){
	union(){
		difference(){
			cube ([w+2*wall_size,d+2*wall_size,h-1], center =false); //h -lip size
			translate ([wall_size,wall_size,1]) cube ([w,d,pcb_h+0.1], center =false);

		// rounding corners
		translate([w+2*wall_size-rad,d+2*wall_size-rad]) rotate(0) createSubtractor(h,rad); 
		translate([rad,d+2*wall_size-rad]) rotate(90) createSubtractor(h,rad);
		translate([rad,rad]) rotate(180) createSubtractor(h,rad);
		translate([w+2*wall_size-rad,rad]) rotate(270) createSubtractor(h,rad);

// pass-through sensor dia 24
		translate([wall_size+w/2, wall_size+d/2, 0]) cylinder(h=wall_size, r=24/2,$fn=25);

	}// difference

  //2x stand-offs for PCB support 3x3x3
	 translate([wall_size+w/2,wall_size+3/2,2]) cube([3,3,2], center =true);
	 translate([wall_size+w/2,wall_size+d-3/2,2]) cube([3,3,2], center =true);

//counter lip 1.5mm
	translate([wall_size-1.4, wall_size-1.4,h-1.5]) lip(1.4,1.4);
	
} //union

} //color ("red") case_a();



// render in assembled form
if(assy==true && dxf==false) {
	translate([w+2*wall_size,0,2*h]) rotate (a=180, v=[0,1,0]) color ("red") case_a(); 
	case_b();
}


//dxf render

if(dxf==true){
	projection (cut=true) translate([w+2*wall_size+gap,0, -6.5]) case_a();
	projection (cut=true) translate ([0,0,-6.5]) case_b();
}

translate([w+2*wall_size+gap,0, 0]) color ("red") case_a	();
case_b();

