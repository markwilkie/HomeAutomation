
/*
set height, length and
width of box here
practical limts are:
> 10 for the height and length
> 15 for  width
values are in millimeters

note holes will ruin
smaller boxes

for larger boxes holes
will not be well placed
but additional holes can
be added

*/

 height_box =33;
length_box =86;
width_box = 50;


 echo(" height box is  ", height_box , " mm");
 echo(" length box is ", length_box, " mm");
 echo(" width box is ", width_box, " mm");


/* follwing 4 lines are parameters for the generation
of the solid hexagons - if rbase and rtop were equal
to 2 the hexagons generated would fill the array
*/


zbase =-2;
ztop   =2;
rbase =1.4;
rtop   =1.4;




 hstart = 35 - height_box;
 wb = width_box/2;








rotate([90,0,0])




difference()
{




union()
{

translate([-wb,0,0])
cube(size=[width_box,35-hstart,2]);

translate([-wb,0,length_box-2])
cube(size=[width_box,30-hstart,2]);

translate([-wb,33-hstart,0])
cube(size=[width_box,2,5]);

translate([-wb,28-hstart,0])
cube(size=[width_box,2,4]);

translate ([0,-hstart,0])
slide(length_box);
 }

//  comment out the following 4 lines
//   to get a box with a solid bottom


rotate([-90,0,0])
translate([-8,-35,0])
scale([2,2,3])
hex_array();


 scale([2,2,70])
translate([-2.5,-1.7,0])
 hex_arr_01();


scale([20,2,2])
translate([0,1,7])
rotate([0,0,90])
hex_arr_02();


}


module slide(ht)
{
linear_extrude($fn=100,height=ht)


// cross section slide box

polygon (points = [

              [-wb,35],[-wb+4.5,35],[-wb+4.5,33],[-wb+2,33],
               [-wb+2,30],[-wb+4.5,30],[-wb+4.5,28],[-wb+2,28],
                [-wb+2,hstart + 2],[wb-2,hstart + 2],
              [wb-2,28],[wb-4.5,28],
                 [wb-4.5,30],[wb-2,30],[wb-2,33],[wb-4.5,33],
                 [wb-4.5,35],[wb,35],[wb,hstart],[-wb,hstart]

]);


       }







module hex_array()
{





 for ( y = [8*sqrt(3),6*sqrt(3),4*sqrt(3),2*sqrt(3),0,-2*sqrt(3),-4*sqrt(3),-6*sqrt(3),-8*sqrt(3),-10*sqrt(3)])
{
 for (x = [-4,2,8])
{
     translate([x,y,0])
         hexagon();
    }

    }


for ( y = [-9*sqrt(3),-7*sqrt(3),-5*sqrt(3),-3*sqrt(3),-sqrt(3),sqrt(3),3*sqrt(3),5*sqrt(3),7*sqrt(3)])
{
 for (x = [-1,5,11])
{
     translate([x,y,0])
       hexagon();
    }

    }

}





module hex_arr_01()
{

union()
{



 for ( y = [6*sqrt(3),4*sqrt(3),2*sqrt(3),0,-2*sqrt(3),-4*sqrt(3)])
{
 for (x = [8])
{
     translate([y,x,0])
        rotate([0,0,30])
         hexagon();
    }

    }


for ( y = [-3*sqrt(3),-sqrt(3),sqrt(3),3*sqrt(3),5*sqrt(3),7*sqrt(3)])
{
 for (x = [5,11])
{
     translate([y,x,0])
      rotate([0,0,30])
       hexagon();
    }

    }
}

}



module hex_arr_02()
{

union()
{



 for ( y = [4*sqrt(3),2*sqrt(3)])
{
 for (x = [-4,2,8,14,20,26])
{
     translate([y,0,x])
        rotate([90,30,0])
         hexagon();
    }

    }


for ( y = [sqrt(3),3*sqrt(3),5*sqrt(3)])
{
 for (x = [-1,5,11,17,23,29])
{
     translate([y,0,x])
      rotate([90,30,0])
       hexagon();
    }

    }
}
}


module hexagon()
{
polyhedron
       (points = [
        [ rbase*cos(0*(360/6)), rbase*sin(0*(360/6)),zbase],
        [ rbase*cos(1*(360/6)), rbase*sin(1*(360/6)),zbase],
        [ rbase*cos(2*(360/6)), rbase*sin(2*(360/6)),zbase],
        [ rbase*cos(3*(360/6)), rbase*sin(3*(360/6)),zbase],
        [ rbase*cos(4*(360/6)), rbase*sin(4*(360/6)),zbase],
        [ rbase*cos(5*(360/6)), rbase*sin(5*(360/6)),zbase],
        [ rtop*cos(0*(360/6)), rtop*sin(0*(360/6)),ztop],
        [ rtop*cos(1*(360/6)), rtop*sin(1*(360/6)),ztop],
        [ rtop*cos(2*(360/6)), rtop*sin(2*(360/6)),ztop],
        [ rtop*cos(3*(360/6)), rtop*sin(3*(360/6)),ztop],
        [ rtop*cos(4*(360/6)), rtop*sin(4*(360/6)),ztop],
        [ rtop*cos(5*(360/6)), rtop*sin(5*(360/6)),ztop],

       ],
           triangles =[
                              [0,4,5],[0,3,4],[0,2,3],[0,1,2],
                             [10,6,11],[9,6,10],[8,6,9],
                              [7,6,8],[0,11,6],[0,5,11],
                              [5,10,11],[5,4,10],
                              [4,9,10],[4,3,9],[3,8,9],[3,2,8],
                              [2,7,8],[2,1,7],[1,6,7],[1,0,6],
              ]
              );
 }






