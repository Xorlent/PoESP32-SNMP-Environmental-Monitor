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
This project produces a SNMPv1/2 temperature and humidity monitoring device with flashed configuration settings and no remote management capability.  Some would see this as a positive from a security-perspective, but it could prove challenging in network environments where change is constant.  A re-flash/re-programming is required to change any configuration options:
- Device IP and subnet
- IP gateway
- SNMP read community string
- Authorized SNMP monitoring node IP address list


__Bottom line: If you need SNMPv3 or desire web management and/or SNMP write functionality, you could enhance this project's code or simply purchase a commercial product.__

## Programming
_Once you've successfully programmed a single unit, this process takes less than 15 minutes from start-to-finish_
1. Set up your Arduino programming environment
2. Disassemble the PoESP32 case
3. Connect header pins on the back side of the mainboard to a USB-to-serial adapter set to 3.3v
   - Ensure RX -> TX and TX -> RX
   - Connect VCC to the 3.3v output on the USB-to-serial adapter
   - Connect GND to the ground from the USB-to-serial adapter
> [!IMPORTANT]
> Do not plug the PoESP32 device into Ethernet until step 11 or you risk damaging your USB port!
3. Plug in the USB while grounding the G0 (GPIO 0) pin if using your own USB-to-serial adapter
   - NOTE: _Grounding G0 is __not__ required if using M5Stack ESP32 Downloader kit_
   - The USB-to-serial ground pin or the ESP32 chip casing provide a good ground if needed
5. The device is now in bootloader mode
6. In Arduino, open the project file and select the USB-to-serial adapter
7. Configure ...................
8. Write the program to the device
9. Disconnect the USB-to-serial adapter and reassemble the case
10. Plug in the ENV IV sensor unit
11. Connect the PoESP32 to a PoE network port
12. Configure your monitoring platform as appropriate
    - Paessler (PRTG) produce a great freely-downloadable SNMP tester for Windows, available [here](https://www.paessler.com/tools/snmptester)
