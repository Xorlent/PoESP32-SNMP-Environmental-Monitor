# The $32 SNMP Environmental Monitor
![PoESP32 Animated Image](https://github.com/Xorlent/PoESP32-SNMP-Environmental-Monitor/blob/main/images/PoESP32-Title.gif)
## Background
With Vertiv unceremoniously stop-shipping and then discontinuing the [Geist Watchdog 15](https://www.vertiv.com/en-us/products-catalog/monitoring-control-and-management/monitoring/watchdog-15/#/benefits-features) during our deployment, we had to scramble to find a suitable equivalent device.  We were shocked to find a lack of decent options for small form-factor, PoE-powered devices that were not astronomically priced.  With M5Stack's PoE-powered ESP32 device in hand, we developed a network SNMP environmental monitor with a total all-in cost of less than 20% of competing products.  The IP101G onboard Ethernet chip is not supported by Arduino Ethernet, thus no existing SNMP library would work for this device, so we wrote a purpose-built SNMP parser for this project.

## Requirements
1. M5Stack [PoESP32 device](https://shop.m5stack.com/products/esp32-ethernet-unit-with-poe), currently $25.90 USD
2. M5Stack [ENV IV sensor unit](https://shop.m5stack.com/products/env-iv-unit-with-temperature-humidity-air-pressure-sensor-sht40-bmp280), currently $5.95 USD
3. A single [M5Stack ESP32 Downloader kit](https://shop.m5stack.com/products/esp32-downloader-kit), currently $9.95 USD

## Cost Analysis
One-time cost for a USB-to-serial device: $9.95 plus tax and shipping  
Total cost per unit: $31.85 plus tax and shipping  
Programming time per unit: < 10 minutes  

## Device Cost Comparison (temperature/humidity only)
- $32: this PoESP32-based device
- $220 before stop-ship: Vertiv Watchdog 15P (discontinued)
- $190 on sale: AKCP sensorProbe1+ Pro
- $315: NTI E-MICRO-TRHP
- $199: MONNIT PoE-X Temperature
- $295: Room Alert 3S

## Device Capability Comparison
This project produces a SNMPv1/2c temperature and humidity monitoring device with flashed configuration settings and no remote management capability.  Some would see this as a positive from a security-perspective, but it could prove challenging in network environments where change is constant.  A re-flash/re-programming is required to modify any configuration options:
- Host name
- Device IP and subnet
- IP gateway
- SNMP read community string
- Authorized SNMP monitoring node IP address list

__Bottom line: If you need SNMPv3 or desire web management and/or SNMP write functionality, you could enhance this project's code or simply purchase a commercial product.__

## Programming
_Once you've successfully programmed a single unit, skip step 1.  Repeating this process takes 5 minutes from start to finish._
1. [Set up your Arduino programming environment](https://github.com/Xorlent/PoESP32-SNMP-Environmental-Monitor/blob/main/ARDUINO-SETUP.md)
2. Disassemble the PoESP32 case
   - You will need a 1.5mm (M2) allen wrench to remove a single screw [pic](https://github.com/Xorlent/PoESP32-SNMP-Environmental-Monitor/blob/main/images/1-Allen.jpg)
   - Inserting a small flat head screwdriver into the slots flanking the Ethernet jack [pic](https://github.com/Xorlent/PoESP32-SNMP-Environmental-Monitor/blob/main/images/2-Slots.jpg), carefully separate the case halves; work it side by side to avoid damage [pic](https://github.com/Xorlent/PoESP32-SNMP-Environmental-Monitor/blob/main/images/3-Tabs.jpg)
> [!TIP]
> If you have fingernails, it can be quicker to slide a nail between the case halves, starting with the end opposite the Ethernet port and using another nail to pull the retaining tabs back
3. In Arduino, open the project file (PoESP32-SNMP-Sensor.ino)
   - Edit the hostname, IP address, subnet, gateway, SNMP read community, and authorized hosts lists at the very top of the file.
   - Select Tools->Board->esp32 and select "ESP32 Dev Module"
4. With the USB-to-serial adapter unplugged, insert the pins in the correct orientation on the back of the PoESP32 mainboard [pic](https://github.com/Xorlent/PoESP32-SNMP-Environmental-Monitor/blob/main/images/4-Programmer.jpg)
> [!WARNING]
> Do not plug the PoESP32 device into Ethernet until after step 7 or you risk damaging your USB port!
5. With light tension applied to ensure good connectivity to the programming through-hole vias on the PoESP32 (see step 4 pic), plug in the USB-to-serial adapter
   - The device is now in bootloader mode
6. In Arduino
   - Select Tools->Port and select the USB-to-serial adapter
     - If you're unsure, unplug the USB-to-serial adapter, look at the port list, then plug it back in and select the new entry (repeating step 5)
   - Select Sketch->Upload to flash the device
   - When you see something similar to the following, proceed to step 7
     ```
     Writing at 0x000d0502... (100 %)
     Wrote 790896 bytes (509986 compressed) at 0x00010000 in 8.9 seconds (effective 714.8 kbit/s)...
     Hash of data verified.

     Leaving...
     Hard resetting via RTS pin...
7. Disconnect the USB-to-serial adapter and reassemble the case
8. Plug in the ENV IV sensor unit [pic](https://github.com/Xorlent/PoESP32-SNMP-Environmental-Monitor/blob/main/images/5-Assembled.jpg)
9. Connect the PoESP32 to a PoE network port and mount as appropriate
   - The holes in the PoESP32 and ENV IV sensor cases work great with zip ties for rack install or screws if attaching to a backboard
   - Do not mount the ENV IV directly on top of the PoESP32, as it generates enough heat to affect sensor readings
10. Configure your monitoring platform as appropriate
    - A list of valid OIDs this sensor will respond to can be found [here](https://github.com/Xorlent/PoESP32-SNMP-Environmental-Monitor/blob/main/OIDINFO.md)
    - Paessler (PRTG) produce a great freely-downloadable SNMP tester for Windows, available [here](https://www.paessler.com/tools/snmptester)
    - If you have PRTG, pre-configured device templates are available for this project at https://github.com/Xorlent/PRTG-OIDLIBS

## Guidance and Limitations
- For monitoring, configure one OID per sensor.  This custom SNMP parser will only respond to one OID per request.
- If you receive a "General Failure" when requesting a valid measurement OID, this means the device is having trouble communicating with the temperature/humidity sensor.
- If you request an invalid OID, expect no response.  The device will not process packets for requests that are not authorized or not a match for a valid OID.
- For high humidity environments, this device will activate an internal sensor heater under certain conditions to ensure more accurate readings.
- The device will respond to pings from any IP address within the routable network.
- Don't have PoE ports on your network switch?  No problem: https://www.amazon.com/gp/product/B0C239DGJF
- Need a simple solution for mounting the PoESP32 and environmental monitor as a unitized assembly?  Within the /3Dmodels folder you will find:
  1. PoESP32-Environmental-1RU.step : 3D print model to mount the PoESP32 assembly into a 1U rack space (w/optional wire cover)
  2. PoESP32-Environmental-Mini.step : 3D print model for zip tie mounting (space constrained)
  3. PoESP32-Environmental-Mini-Magnet.step : 3D print model for magnet mounting (space constrained, compatible with 8mm x 2mm disc magnets)
  4. PoESP32-Environmental-Mid.step : 3D print model for zip tie mounting
  5. PoESP32-Environmental-Mid-Magnet.step : 3D print model for magnet mounting (compatible with 8mm x 2mm disc magnets)
  6. If you want to modify the models and make your own custom design:
     - [Onshape link - 1RU Base](https://cad.onshape.com/documents/126ed9d0ea20223ee2558e2e/w/34238fabfb355e242dfb51a2/e/70eb0dc678e6fe37b06c7b4d?renderMode=0&uiState=666d1124cd9bd3671768c9e6)
     - [Onshape link - 1RU Cover](https://cad.onshape.com/documents/126ed9d0ea20223ee2558e2e/w/34238fabfb355e242dfb51a2/e/cc43e711478951692c63e341?renderMode=0&uiState=666d1149cd9bd3671768c9fe)
     - [Onshape link - Mini](https://cad.onshape.com/documents/126ed9d0ea20223ee2558e2e/w/f747afb8fc8c6e8e288e0fc9/e/70eb0dc678e6fe37b06c7b4d?renderMode=0&uiState=666d1111cd9bd3671768c9c6)
     - [Onshape link - Mini Magnetic](https://cad.onshape.com/documents/126ed9d0ea20223ee2558e2e/w/bfca9ecb4e85ab436c8e3736/e/70eb0dc678e6fe37b06c7b4d?renderMode=0&uiState=666d10f6cd9bd3671768c9a8)
     - [Onshape link - Mid](https://cad.onshape.com/documents/126ed9d0ea20223ee2558e2e/w/86908f6e4038f162632614a8/e/70eb0dc678e6fe37b06c7b4d?renderMode=0&uiState=666d10c5cd9bd3671768c95a)
     - [Onshape link - Mid Magnetic](https://cad.onshape.com/documents/126ed9d0ea20223ee2558e2e/w/887531df77e994a6e9e16eac/e/70eb0dc678e6fe37b06c7b4d?renderMode=0&uiState=666d10e8cd9bd3671768c99a)

## Technical Information
- Operating Specifications
  - Operating temperature: 0°F (-17.7°C) to 140°F (60°C)
  - Operating humidity: 5% to 90% (RH), non-condensing
- Sensor Accuracy
  - ±0.1 °C，±1.5 %RH
- Power Consumption
  - 6W maximum via 802.3af Power-over-Ethernet
- Ethernet
  - IP101G PHY
  - 10/100 Mbit twisted pair copper
  - IEEE 802.3af Power-over-Ethernet
- I/O Configuration
  - SHT40 temperature and humidity sensor
  - See [PORTINFO.md](https://github.com/Xorlent/PoESP32-SNMP-Environmental-Monitor/blob/main/PORTINFO.md)
