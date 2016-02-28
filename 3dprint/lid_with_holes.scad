
length_box = 86;
width_box = 50;


/* follwing 4 lines are parameters for the generation
of the solid hexagons - if rbase and rtop were equal
to 2 the hexagons generated would fill the array
*/

zbase =-2;
ztop   =2;
rbase =1.5;
rtop   =1.5;



width = width_box - 5;
length = length_box - 3;

difference()
{


union()
{

cube(size=[width,length,2]);
translate([2.5,0,0])
cube(size =[width -5,4.5,4]);

}

translate([15,25,0])
scale([2,2,2])
hex_array();


}





module hex_array()
{





 for ( y = [12*sqrt(3),10*sqrt(3),8*sqrt(3),6*sqrt(3),4*sqrt(3),2*sqrt(3),0,-2*sqrt(3),-4*sqrt(3)])
{
 for (x = [-4,2,8])
{
     translate([x,y,0])
         hexagon();
    }

    }


for ( y = [-5*sqrt(3),-3*sqrt(3),-sqrt(3),sqrt(3),3*sqrt(3),5*sqrt(3),7*sqrt(3),9*sqrt(3),11*sqrt(3),13*sqrt(3)])
{
 for (x = [-1,5,11])
{
     translate([x,y,0])
       hexagon();
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

}


