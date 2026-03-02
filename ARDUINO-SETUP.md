1. Download the latest Arduino IDE appropriate for your operating system (https://www.arduino.cc/en/software)
2. Open the Arduino IDE
3. Open the Preferences window
   - In the "Additional Board Manager URLs" field, paste the following:
   ```https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json```
4. Close/save the Preferences window
5. Open the Boards Manager
   - Type "esp32" in the search field and select "esp32 by Espressif Systems" (__NOT "Arduino ESP32 Boards"__)
     - Install version 2.0.17
6. Open the Libraries Manager
   - Type "sht4x" in the search field and select "Sensirion I2C SHT4x"
     - Install version 1.1.2
   - Type "arduino-timer" in the earch field and select "arduino-timer"
     - Install version 3.0.1
7. Install the CH9102 USB-to-serial drivers ([Windows](https://learn.adafruit.com/how-to-install-drivers-for-wch-usb-to-serial-chips-ch9102f-ch9102/windows-driver-installation)/[MacOS](https://learn.adafruit.com/how-to-install-drivers-for-wch-usb-to-serial-chips-ch9102f-ch9102/mac-driver-installation))
   - Note: These drivers do not appear to work on MacOS 14.5
> [!IMPORTANT]
> This version is only compatible with the esp32 board library version 3.x.  If you must use 2.x, download PoESP32-SNMP-Environmental-Monitor release v1.
