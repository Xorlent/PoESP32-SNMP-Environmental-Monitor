1. Download the latest Arduino IDE appropriate for your operating system (https://www.arduino.cc/en/software)
2. Open the Arduino IDE
3. Click File->Preferences or Arduino IDE->Settings (MacOS)
4. Paste the following into the "Additional board manager URLs" field and click "OK"  
   ```https://espressif.github.io/arduino-esp32/package_esp32_index.json```
6. Open the Boards Manager
   - Type "esp32" in the search field and select "esp32 by Espressif Systems" (__NOT "Arduino ESP32 Boards"__)
     - Install version 3.3.7
7. Open the Libraries Manager
   - Type "sht4x" in the search field and select "SHT4x by Rob Tillaart"
     - Install version 0.1.0
   - Type "arduino-timer" in the search field and select "arduino-timer by Michael Contreras"
     - Install version 3.0.1
8. Install the CH9102 USB-to-serial drivers ([Windows](https://learn.adafruit.com/how-to-install-drivers-for-wch-usb-to-serial-chips-ch9102f-ch9102/windows-driver-installation)/[MacOS](https://learn.adafruit.com/how-to-install-drivers-for-wch-usb-to-serial-chips-ch9102f-ch9102/mac-driver-installation))
   - Note: These drivers do not appear to work on MacOS 14.5
