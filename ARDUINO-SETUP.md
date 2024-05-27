1. Download the latest Arduino IDE appropriate for your operating system (https://www.arduino.cc/en/software)
2. Open the Arduino IDE
3. Open the Preferences window
   - In the "Additional Board Manager URLs" field, paste the following:
   ```https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json```
4. Close/save the Preferences window
5. Open the Boards Manager
   - Type "esp32" in the search field and select "esp32 by Espressif Systems" (__NOT "Arduino ESP32 Boards"__)
     - Install version 2.0.16 (tested) or 2.0.17 (should work based on release notes)
6. Open the Libraries Manager
   - Type "sht4x" in the search field and select "Sensirion I2C SHT4x"
     - Install version 1.1.0
   - Type "arduino-timer" in the earch field and select "arduino-timer"
     - Install version 3.0.1
