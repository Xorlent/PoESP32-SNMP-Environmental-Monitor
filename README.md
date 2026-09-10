# The $32 SNMP Environmental Monitor
![PoESP32 Animated Image](https://github.com/Xorlent/PoESP32-SNMP-Environmental-Monitor/blob/main/images/PoESP32-Title.gif)
## Background
With Vertiv unceremoniously stop-shipping and then discontinuing the [Geist Watchdog 15](https://www.vertiv.com/en-us/products-catalog/monitoring-control-and-management/monitoring/watchdog-15/#/benefits-features) during our deployment, we had to scramble to find a suitable equivalent device.  We were shocked to find a lack of decent options for small form-factor, PoE-powered devices that were not astronomically priced.  With M5Stack's PoE-powered ESP32 device in hand, we developed a network SNMP environmental monitor with a total all-in cost of less than 20% of competing products.

## Requirements
1. M5Stack [PoESP32 device](https://shop.m5stack.com/products/esp32-ethernet-unit-with-poe), or a newer [Unit-PoE-P4](https://shop.m5stack.com/products/unit-poe-with-esp32-p4), both around $25 USD
   - Notes on the Unit-PoE-P4:
     - Using the Unit-PoE-P4 significantly reduces the device operating specification from -17.7°C to 0°C and 60°C to 40°C
     - Requires NO disassembly or downloader kit, just a USB C cable for programming
     - The Unit-PoE-P4 device will not mount to any of the provided 3D-printable models.  I may produce revised designs for this if there is interest.
2. Sensor
   - M5Stack [ENV III sensor unit](https://shop.m5stack.com/products/env-iii-unit-with-temperature-humidity-air-pressure-sensor-sht30-qmp6988), currently $5.95 USD
   - or a custom SHT41-based sensor.  All files and instructions can be found in the /SHT4X folder.  As little as $2.20/each shipped in quantities of 30.
3. A single [M5Stack ESP32 Downloader kit](https://shop.m5stack.com/products/esp32-downloader-kit) (not required for Unit-PoE-P4), currently $9.95 USD

## Cost Analysis
One-time cost for a USB-to-serial device (PoESP32 only): $9.95 plus tax and shipping  
Total cost per unit: $31.85 plus tax and shipping  
Programming time per unit: < 10 minutes  

## Device Overview
This project produces a SNMPv1/2c temperature and humidity monitoring device.  Configuration is set when the device is flashed, and it can optionally be managed from a DHCP server.

## Programming
_Once you've successfully programmed a single unit, skip step 1.  Repeating this process takes less than 5 minutes from start to finish._
1. [Set up your Arduino programming environment](https://github.com/Xorlent/PoESP32-SNMP-Environmental-Monitor/blob/main/ARDUINO-SETUP.md)
2. Disassemble the PoESP32 case (skip if using Unit-PoE-P4)
   - You will need a 1.5mm (M2) allen wrench to remove a single screw [pic](https://github.com/Xorlent/PoESP32-SNMP-Environmental-Monitor/blob/main/images/1-Allen.jpg)
   - Inserting a small flat head screwdriver into the slots flanking the Ethernet jack [pic](https://github.com/Xorlent/PoESP32-SNMP-Environmental-Monitor/blob/main/images/2-Slots.jpg), carefully separate the case halves; work it side by side to avoid damage [pic](https://github.com/Xorlent/PoESP32-SNMP-Environmental-Monitor/blob/main/images/3-Tabs.jpg)
> [!TIP]
> If you have fingernails, it can be quicker to slide a nail between the case halves, starting with the end opposite the Ethernet port and using another nail to pull the retaining tabs back
3. In Arduino, open the project file (PoESP32-SNMP-Environmental-Monitor.ino)
   - Choose your DHCP mode (see the DHCP Provisioning section below) and set the appropriate value for `DHCPControl`
   - Edit the hostname, IP address, subnet, gateway, SNMP read community, and authorized hosts lists at the very top of the file
   - If using DHCP, ensure you set the COMMUNITY_KEY value; the same value must be used on all PoESP32 devices sharing the same DHCP-provided SNMP read community (option 231) value
   - Select Tools->Board->esp32 and select "ESP32 Dev Module" if using PoESP32
   - OR select Tools->Board->esp32 and select "ESP32P4 Dev Module" if using Unit-PoE-P4
     - Set ESP32P4 board parameters according to [this screenshot](https://github.com/Xorlent/PoESP32-SNMP-Environmental-Monitor/blob/main/images/ESP32P4-Config.jpg)
4. With the USB-to-serial adapter unplugged, insert the pins in the correct orientation on the back of the PoESP32 mainboard [pic](https://github.com/Xorlent/PoESP32-SNMP-Environmental-Monitor/blob/main/images/4-Programmer.jpg) (skip if using Unit-PoE-P4)
> [!WARNING]
> Do not plug the device into a powered Ethernet port until after step 7 or you risk damaging your USB port!
5. With light tension applied to ensure good connectivity to the programming through-hole vias on the PoESP32 (see step 4 pic), plug in the USB-to-serial adapter
   - The device is now in bootloader mode
   - For the Unit-PoE-P4, simply plug the USB cable into the USB port above the Ethernet jack
6. In Arduino
   - Select Tools->Port and select the USB-to-serial interface
     - If you're unsure, unplug the USB cable, look at the port list, then plug it back in and select the new entry (repeating step 5)
   - Select Sketch->Upload to flash the device
   - When you see something similar to the following, proceed to step 7.  If you see an error, try lowering the upload speed in the Tools menu.
     ```
     Writing at 0x000d0502... (100 %)
     Wrote 790896 bytes (509986 compressed) at 0x00010000 in 8.9 seconds (effective 714.8 kbit/s)...
     Hash of data verified.

     Leaving...
     Hard resetting via RTS pin...
7. Disconnect the USB cable and reassemble the case if applicable
8. Plug in the sensor accessory [pic](https://github.com/Xorlent/PoESP32-SNMP-Environmental-Monitor/blob/main/images/5-Assembled.jpg)
9. Connect the device to a PoE network port and mount as appropriate
   - The holes in the PoESP32 and ENV IV sensor cases work great with zip ties for rack install or screws if attaching to a backboard
     - See the /3Dmodels folder for print-able mounting plates or [Guidance and Limitations](https://github.com/Xorlent/PoESP32-SNMP-Environmental-Monitor/blob/main/README.md#guidance-and-limitations) for more detail
   - Do not mount the ENV IV directly on top of the PoESP32, as it generates enough heat to affect sensor readings
10. Configure your monitoring platform as appropriate
    - A list of valid OIDs this sensor will respond to can be found [here](https://github.com/Xorlent/PoESP32-SNMP-Environmental-Monitor/blob/main/OIDINFO.md)
    - Paessler (PRTG) produce a great freely-downloadable SNMP tester for Windows, available [here](https://www.paessler.com/tools/snmptester)
    - If you have PRTG, pre-configured device templates are available for this project at https://github.com/Xorlent/PRTG-OIDLIBS
    - Don't have a monitoring platform?  [PRTG Freeware](https://www.paessler.com/free_network_monitor) would support monitoring and alerting for up to 20 of these devices

## DHCP Provisioning (optional)

By default the device uses the settings you compiled into the sketch, allowing values present in valid DHCP server responses to override the compiled values (`DHCP_IFAVAILABLE`).

### Choose a mode

Near the top of the sketch, set `DHCPControl` to one of:

| Mode | Description |
|------|--------------|
| `DHCP_NEVER` | Do not use DHCP.  Use only the settings configured within your sketch. |
| `DHCP_IFAVAILABLE` | Default.  Start from the compiled settings, then let any DHCP options you configure override them.  If the IP, subnet, or gateway changes, the device saves the change and reboots.  If no SNMP request arrives within the revert window (default 15 minutes) after that reboot, it reverts to the previous network settings and reboots again. |
| `DHCP_ALWAYS` | Wait for DHCP to supply everything before starting.  Retries every 10 seconds and prints which required options are still missing in the serial console. |

### What DHCP supplies

The device reads these options from your DHCP server:

| Option | Setting |
|--------|---------|
| IP / 1 / 3 | IP address, subnet mask, gateway |
| 12 | Hostname |
| 230 | Authorized SNMP hosts (4-byte IPv4 addresses, up to 8) |
| 231 | SNMP read community (encrypted — see "Community encryption" below) |

### SNMP Read Community encryption

The read community is never transmitted in cleartext over DHCP.

#### Generating an encrypted read community value

1. Set `COMMUNITY_KEY` in the sketch to a shared secret (the same string on every device).
2. Once programmed, on any device's serial console, type `G` + Enter, then type the read community string you want to use (e.g. `readonly`).
3. The device prints the encrypted value you can then paste into the DHCP server's option 231 string.

The device encrypts the community with ChaCha20 (keyed by `COMMUNITY_KEY`) and hex-encodes the result.  On boot it decrypts option 231 back into the community, so the cleartext value never crosses the LAN.  Because the key and the community are shared fleet-wide, running `G` once produces a value that works for every device.

### Prepping a Windows DHCP server

1. Open DHCP Management
2. Right-click IPv4 and select `Set Predefined Options`
3. Click `Add...`
4. Enter the values as shown below and click `OK`  
![Add SNMP Hosts](https://github.com/Xorlent/PoESP32-SNMP-Environmental-Monitor/blob/DHCP-Support/images/AddSNMPHosts.jpg)
5. Click `Add...`
6. Enter the values as shown below and click `OK`  
![Add SNMP Read Community](https://github.com/Xorlent/PoESP32-SNMP-Environmental-Monitor/blob/DHCP-Support/images/AddSNMPReadCommunity.jpg)
7. You can now set Scope Option values for Option 230 (authorized hosts) and option 231 (the encrypted read community from the "G" command — see "Community encryption" above)

### Setting up a DHCP reservation (DHCP_ALWAYS or DHCP_IFAVAILABLE)

1. With the device connected to serial, power it on and note its MAC address (printed at startup).
2. Create a DHCP reservation for that MAC address on your DHCP server.
3. On the reservation, add option 12 (Hostname) if desired.

### Tuning

- `DHCPQueryInterval` is how often (in seconds) the device re-checks DHCP.  The device never waits longer than half the lease time, so a short lease won't be lost.  Set it to `0` to rely only on the lease time.
- `authorizedSNMPOption` and `readCommunityOption` change which option numbers carry the authorized-host list and read community (defaults 230 and 231).
- `revertWindowMinutes` is how long (in minutes) the device waits for an SNMP request after a network change before reverting (default 15).
- `COMMUNITY_KEY` is the shared secret used to encrypt the community (with the `G` command).  Keep it the same on every device; it must be at least 16 characters.

## Guidance and Limitations
- For monitoring, configure one OID per sensor.  This custom SNMP parser will only respond to one OID per request.
- If you receive a "General Failure" when requesting a valid measurement OID, this means the device is having trouble communicating with the temperature/humidity sensor.
- If you request an invalid OID, expect no response.  The device will not process packets for requests that are not authorized or not a match for a valid OID.
- For high humidity environments, this device will activate an internal sensor heater under certain conditions to ensure more accurate readings.
- The device will respond to pings from any IP address within the routable network.
- Don't have PoE ports on your network switch?  No problem: https://www.amazon.com/gp/product/B0C239DGJF
- Need a simple solution for mounting the PoESP32 and environmental monitor as a unitized assembly?  Within the /3Dmodels folder you will find:
  1. PoESP32-Environmental-1RU-Base.step : 3D print model to mount the PoESP32 assembly into a 1U rack space (w/optional wire cover and LED lightguides)
  2. PoESP32-Environmental-Mini.step : 3D print model for zip tie mounting (space constrained)
  3. PoESP32-Environmental-Mini-Magnet.step : 3D print model for magnet mounting (space constrained, compatible with 8mm x 2mm disc magnets)
  4. PoESP32-Environmental-Mid.step : 3D print model for zip tie mounting
  5. PoESP32-Environmental-Mid-Magnet.step : 3D print model for magnet mounting (compatible with 8mm x 2mm disc magnets)

## Technical Information
- Operating Specifications
  - Operating temperature: 0°F (-17.7°C) to 140°F (60°C)
  - Operating humidity: 5% to 90% (RH), non-condensing
- Sensor Accuracy
  - ±0.2 °C, ±1.8 %RH
- Power Consumption
  - 6W maximum via 802.3af Power-over-Ethernet
- Ethernet
  - IP101G PHY
  - 10/100 Mbit twisted pair copper
  - IEEE 802.3af Power-over-Ethernet
- I/O Configuration
  - SHT40 temperature and humidity sensor
  - See [PORTINFO.md](https://github.com/Xorlent/PoESP32-SNMP-Environmental-Monitor/blob/main/PORTINFO.md)
