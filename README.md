# PoESP32-SNMP-Environmental-Monitor
![PoESP32 Picture](https://github.com/Xorlent/PoESP32-SNMP-Environmental-Monitor/blob/main/images/ESPoE32.jpg)
## Background
With Vertiv unceremoniously stop-shipping and then discontinuing [Geist Watchdog 15 units](https://www.vertiv.com/en-us/products-catalog/monitoring-control-and-management/monitoring/watchdog-15/#/benefits-features), we had to scrable to find a suitable equivalent device.  We were shocked to find a lack of decent options for small form-factor, PoE-powered devices that were not astronomically priced.  With M5Stack's PoE-powered ESP32 device in hand, we developed a network SNMP environmental monitoring device with a total all-in cost of less than 20% of competing products.

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
- Host name
- Device IP and subnet
- IP gateway
- SNMP read community string
- Authorized SNMP monitoring node IP address list

__Bottom line: If you need SNMPv3 or desire web management and/or SNMP write functionality, you could enhance this project's code or simply purchase a commercial product.__

## Programming
_Once you've successfully programmed a single unit, skip step 1.  This process takes less than 10 minutes from start to finish_
1. [Set up your Arduino programming environment](https://github.com/Xorlent/PoESP32-SNMP-Environmental-Monitor/blob/main/ARDUINO-SETUP.md)
2. Disassemble the PoESP32 case
   - You will need a 1.5 mm allen wrench to remove a single screw
   - Inserting a small flat head screwdriver into the slots flanking the Ethernet jack, carefully separate the case halves; work it side by side to avoid damage
3. In Arduino, open the project file (PoESP32-SNMP-Sensor.ino)
   - Edit the hostname, IP address, subnet, gateway, SNMP read community, and authorized hosts lists at the very top of the file.
4. With the USB-to-serial adapter unplugged, insert the pins in the correct orientation on the back of the PoESP32 mainboard
   - _PICTURE COMING SOON..._
> [!IMPORTANT]
> Do not plug the PoESP32 device into Ethernet until step 9 or you risk damaging your USB port!
5. With light tension applied to ensure good connectivity to the programming through-hole vias on the PoESP32, plug in the USB-to-serial adapter
   - The device is now in bootloader mode
6. In Arduino
   - Select Tools->Port and select the USB-to-serial adapter
     - If you're unsure, unplug the USB-to-serial adapter, look at the port list, then plug it back in and select the new entry (repeating step 3)
   - Select Sketch->Upload to flash the device
7. Disconnect the USB-to-serial adapter and reassemble the case
8. Plug in the ENV IV sensor unit
9. Connect the PoESP32 to a PoE network port and mount as appropriate
10. Configure your monitoring platform as appropriate
    - A list of valid OIDs this sensor will respond to can be found [here](https://github.com/Xorlent/PoESP32-SNMP-Environmental-Monitor/blob/main/OIDINFO.md)
    - Paessler (PRTG) produce a great freely-downloadable SNMP tester for Windows, available [here](https://www.paessler.com/tools/snmptester)

## Guidance and Limitations
- For monitoring, configure one OID per sensor.  This custom SNMP parser will only respond to one OID per request.
- If you receive a "General Failure" when requesting a valid OID, this means the device is having trouble communicating with the temperature/humidity sensor.
- For high humidity environments, this device will activate an internal sensor heater under certain conditions to ensure accurate readings.

## Technical Information
- Operating specifications
  - Operating temperature: 0°F (-18°C) to 127°F (53°C)
  - Operating humidity: 5% to 90% (RH), non-condensing
- For those interested in the I/O configuration of the M5Stack PoESP32 device, port info is [here](https://github.com/Xorlent/PoESP32-SNMP-Environmental-Monitor/blob/main/PORTINFO.md)
