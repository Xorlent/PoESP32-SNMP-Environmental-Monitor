# PoESP32-SNMP-Environmental-Monitor

## Background
With Vertiv unceremoniously stop-shipping and then discontinuing [Geist Watchdog 15 units](https://www.vertiv.com/en-us/products-catalog/monitoring-control-and-management/monitoring/watchdog-15/#/benefits-features), we had to scrable to find a suitable equivalent device.  We were shocked to find a lack of decent options for small form-factor, PoE-powered devices that were not astronomically priced.  With M5Stack's PoE-powered ESP32 device in hand, we developed a network SNMP environmental monitoring device with a total all-in cost of less than 25% of competing products.

## Requiremements
1. M5Stack [PoESP32 device](https://shop.m5stack.com/products/esp32-ethernet-unit-with-poe), currently $25.90 USD
2. M5Stack [ENV IV sensor unit](https://shop.m5stack.com/products/env-iv-unit-with-temperature-humidity-air-pressure-sensor-sht40-bmp280), currently $5.95 USD
3. A single USB-to-serial device or the [M5Stack ESP32 Downloader kit](https://shop.m5stack.com/products/esp32-downloader-kit), currently $9.95 USD

## Cost Analysis
One-time cost for a USB-to-serial device: $9.95 plus tax and shipping  
Total cost per unit: $31.85 plus tax and shipping  
Programming time per unit: < 20 minutes  

## Device Cost Comparison (temperature/humidity only)
- $32: PoESP32-based device
- $220 before stop-ship: Vertiv Watchdog 15P (discontinued)
- $190 on sale: AKCP sensorProbe1+ Pro
- $315: NTI E-MICRO-TRHP
- $199: MONNIT PoE-X Temperature
- $295: Room Alert 3S

## Device Capability Comparison
This project produces a device with flashed configuration settings and no remote management capability.  Some would see this as a positive from a security-perspective, but it could prove challenging in network environments where change is constant.  A re-flash/re-programming is required to change any of the following configuration options:
- Device IP and Subnet
- IP Gateway
- Authorized SNMP monitoring node IP address list
- SNMP read community string

__Bottom line: If you need or desire web management and/or SNMP write functionality, you could enhance this project's code or simply purchase a commercial product.__

## Programming
_Once you've successfully programmed a single unit, this process takes less than 10 minutes from start-to-finish_
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
