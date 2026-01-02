# Assembly

How to create the project?

## Parts

See [`BOM.md`](./BOM.md) for the parts you need.

## 3D print

Download the Onshape, convert the parts to different STL
(or remind me to put the STL here directly) then 3D-print them.

## PCB

Use the Gerber files to make the PCB.
To mount it, first solder pins on the 3 vias of the PCB where the
microcontroller will be.

You then need to solder 2 wires on the Batt+ and Batt- on the back of the
microcontroller. Don't forget to hotglue / epoxy them so the pad doesn't
detach under load.
You can now pass those wires throught the big hole of the PCB, and put the
microcontroller in the correct pins.

Finally solder all remaining pads then the other components.

For the capacitive touch sensors, solder them first then fix them at the desired
height and angle.
