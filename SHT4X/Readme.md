## This folder contains a temperature and humidity sensor that can be used in place of the now end-of-life M5Stack ENV-IV
1. A full PCB design that can be produced by JLCPCB for as little as $2.20 each in quantities of 30
   - Upload the SHT41A.zip file to JLCPCB accepting all defaults
   - Select economic assembly service, top side only
   - Click "Next"
   - Upload SHT41A-BOM.csv for the "Bill of Materials" list
   - Upload SHT41A-all-pos.csv for the Pick & Place file
   - Click "Process BOM & CPL"
   - Ensure components match the BOM file (you can open this file in a text editor to verify results)  
![SHT4X Board View](https://github.com/Xorlent/PoESP32-SNMP-Environmental-Monitor/blob/main/SHT4X/SHT4X%20Board%20View.png)
![SHT4X Board Assembly](https://github.com/Xorlent/PoESP32-SNMP-Environmental-Monitor/blob/main/SHT4X/SHT4X%20Board%20Assembly.png)

2. A 3D-printable M5Stack sensor module compatible case
   - Print in PETG using .25mm layer height, .45mm extrusion width, 15% gyroid infill
   - Insert the assembled PC Board into the Bottom Shell
   - Using Weld-On #4 Cement, apply a small amount to the Top Shell barrels and the slots next to the connector opening
   - Assemble the shell halves, holding the top and bottom shells together for a few minutes

![SHT4X Sensor Module](https://github.com/Xorlent/PoESP32-SNMP-Environmental-Monitor/blob/main/SHT4X/SHT4X%20Assembly.png)

