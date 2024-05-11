# PoESP32-SNMP-Environmental-Monitor

1. Disassemble the POESP32 case
2. Connect header pins on the back side of the mainboard to a USB-to-serial adapter set to 3.3v
  - Ensure RX -> TX and TX -> RX
3. Plug in the USB while grounding the G0 (GPIO 0) pin
  - The ESP32 chip casing provides a good ground
4. The device is now in bootloader mode
5. In Arduino, open the project file and select the USB-to-serial adapter
6. Configure ...................
7. Write the program to the device
8. Configure your monitoring platform as appropriate
   - Paessler, the developers of PRTG, have a great freely-downloadable SNMP tester for Windows, available [here](https://www.paessler.com/tools/snmptester)
