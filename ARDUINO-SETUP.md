1. Download the latest Arduino IDE appropriate for your operating system (https://www.arduino.cc/en/software)
2. Open the Arduino IDE
3. Open the Boards Manager
   - Type "esp32" in the search field and select "esp32 by Espressif Systems" (__NOT "Arduino ESP32 Boards"__)
     - Install version 3.3.7
4. Open the Libraries Manager
   - Type "sht4x" in the search field and select "SHT4x by Rob Tillaart"
     - Install version 0.1.0
   - Type "arduino-timer" in the search field and select "arduino-timer by Michael Contreras"
     - Install version 3.0.1
5. Install the CH9102 USB-to-serial drivers ([Windows](https://learn.adafruit.com/how-to-install-drivers-for-wch-usb-to-serial-chips-ch9102f-ch9102/windows-driver-installation)/[MacOS](https://learn.adafruit.com/how-to-install-drivers-for-wch-usb-to-serial-chips-ch9102f-ch9102/mac-driver-installation))
   - Note: These drivers do not appear to work on MacOS 14.5
> [!IMPORTANT]
> Do not allow Arduino IDE to update the esp32 board library to any 3.x version, as there are breaking Ethernet changes in this new release.
