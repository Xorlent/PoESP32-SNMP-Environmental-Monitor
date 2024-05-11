# PoESP32-SNMP-Environmental-Monitor

## Background


## Requiremements
1. M5Stack PoESP32 device (https://shop.m5stack.com/products/esp32-ethernet-unit-with-poe)
2. M5Stack ENV IV sensor unit (https://shop.m5stack.com/products/env-iv-unit-with-temperature-humidity-air-pressure-sensor-sht40-bmp280)
3. A USB-to-serial device or the M5Stack ESP32 Downloader kit (https://shop.m5stack.com/products/esp32-downloader-kit)

## Programming
1. Disassemble the PoESP32 case
2. Connect header pins on the back side of the mainboard to a USB-to-serial adapter set to 3.3v
   - Ensure RX -> TX and TX -> RX
3. Plug in the USB while grounding the G0 (GPIO 0) pin if using your own USB-to-serial adapter (not required if using M5Stack ESP32 Downloader kit)
   - The ESP32 chip casing provides a good ground
4. The device is now in bootloader mode
5. In Arduino, open the project file and select the USB-to-serial adapter
6. Configure ...................
7. Write the program to the device
8. Disconnect the USB-to-serial adapter and reassemble the case
9. Plug in the ENV IV Unit (https://shop.m5stack.com/products/env-iv-unit-with-temperature-humidity-air-pressure-sensor-sht40-bmp280)
10. Connect the PoESP32 to a PoE network port
11. Configure your monitoring platform as appropriate
    - Paessler, the developers of PRTG, have a great freely-downloadable SNMP tester for Windows, available [here](https://www.paessler.com/tools/snmptester)
