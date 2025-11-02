# ESP8266 Dual-Unit Smart Thermostat (v12)

This is an advanced, dual-ESP8266 smart thermostat project. It consists of two main parts:
1.  **Main Controller (NodeMCU V3):** Acts as the central hub. It reads local temperature, controls the boiler relay, connects to WiFi for time/weather, and displays all information on an OLED screen.
2.  **Remote Sensor (ESP-01S):** A battery-powered, deep-sleep sensor that wakes up periodically to measure the temperature of a radiator (using a DS18B20) and transmits it back to the main controller via ESP-NOW.

The main controller uses this remote sensor data for its "Continuous" heating mode, allowing for more intelligent control based on actual radiator heat rather than just ambient air temperature.

**Note:** This project is currently in development. The code is functional but under continuous improvement.

## Key Features

* **Dual-Unit Communication:** Uses `ESP-NOW` for low-latency, reliable communication between the remote sensor and the main controller.
* **Three Operating Modes:**
    1.  **Thermostat:** Standard operation based on the local ambient temperature sensor (AHT10).
    2.  **Continuous (Radiator):** Advanced mode that regulates the boiler based on the remote radiator temperature received from the ESP-01S.
    3.  **Off:** Boiler relay is kept off.
* **Hybrid Sensing:**
    * **Main Unit (Local):** `Adafruit AHT10` (I2C) for ambient room temperature and humidity.
    * **Remote Unit (Radiator):** `Dallas DS18B20` (OneWire) for precise radiator temperature.
* **Smart Connectivity:**
    * Recovers exact time via `NTP` client.
    * Fetches external weather data (temp, feels like, humidity, wind) from the OpenWeatherMap API.
    * Manages WiFi and ESP-NOW coexistence: WiFi is activated only for periodic syncs to avoid interfering with ESP-NOW.
* **Remote Sensor Power-Saving:**
    * The ESP-01S operates in `deep-sleep` to maximize battery life.
    * Wakes every 10 minutes (or 6 minutes if ACK fails) to send data.
    * Supports a remote "long sleep" command (e.g., `SLEEP_LONG_X`) sent from the main unit, using RTC memory to sleep for multiple hours (e.g., overnight).
* **User Interface (Main Unit):**
    * `OLED SH1106G` (128x64) display for status and menus.
    * `Rotary Encoder` with push-button for menu navigation and settings adjustment.
* **Persistence:** Saves settings (mode, target temperatures) to the `LittleFS` flash memory on the NodeMCU.
* **Statistics:** Tracks total boiler 'ON' time, with a daily reset.

---

## Project Structure

This repository contains the code for both microcontrollers:

* `ESP8266_code_v12.txt`: The firmware for the **Main Controller (NodeMCU V3)**.
* `esp01s_code_v5.txt`: The firmware for the **Remote Sensor (ESP-01S)**.

---

## Hardware Required

### Main Controller (Receiver)

* **Controller:** NodeMCU V3 (ESP8266).
* **Display:** 1.3" I2C OLED Display (128x64) with `SH1106G` driver.
* **Local Sensor:** `Adafruit AHT10` (I2C) temperature/humidity sensor.
* **Input:** `KY-040` Rotary Encoder module with push-button.
* **Actuator:** 1-Channel Relay Module to control the boiler.

### Remote Sensor (Transmitter)

* **Controller:** `ESP-01S` (ESP8266).
* **Remote Sensor:** `Dallas DS18B20` (OneWire) waterproof temperature probe.
* **Other:** 4.7k Ohm pull-up resistor (for DS18B20), and a battery power-supply (e.g., LiPo or 2xAAA).

---

## Libraries & Dependencies

Ensure these libraries are installed via your Arduino IDE's Library Manager.

### Main Controller (NodeMCU)

* `Adafruit_SH110X`
* `ESP8266WiFi` (core)
* `ESP8266HTTPClient` (core)
* `espnow.h` (core)
* `Adafruit AHT10`
* `Wire.h` (core)
* `Adafruit_GFX`
* `WiFiUdp` (core)
* `NTPClient`
* `LittleFS` (core)

### Remote Sensor (ESP-01S)

* `ESP8266WiFi` (core)
* `espnow.h` (core)
* `OneWire`
* `DallasTemperature`

---

## Configuration

You **must** edit the following variables in both files before uploading:

### 1. Main Controller (`ESP8266_code_v12.txt`)

// WiFi Credentials
const char *ssid = ""; // insert your network SSID
const char *password = ""; // insert your network password

// OpenWeatherMap API URL
String weatherURL = ""; // insert your full API URL (e.g., "api.openweathermap.org/data/2.5/weather?q=CITY&appid=YOUR_KEY&units=metric")

// MAC Address of your Remote Sensor (ESP-01S)
uint8_t esp01s_mac[] = { ***insert your ESP-01S mac address*** };

// Timezone Offset
const long utcOffsetInSeconds = 3600; // e.g., 3600 for +1 UTC

// MAC Address of your Main Controller (NodeMCU)
uint8_t nodeMCUMAC[] = {***insert your NodeMCU mac address***}; // MAC del NodeMCU

// OneWire Bus Pin (default is GPIO 0 for ESP-01S)
OneWire dallas(0);

## System Logic

### Connectivity (WiFi vs. ESP-NOW)
The ESP8266 struggles to run ESP-NOW and standard WiFi (STA) simultaneously. This project prioritizes the ESP-NOW link.

ESP-NOW is the primary state, used for sensor communication.

WiFi is activated only when needed (on boot, and every sync_interval of ~30 minutes) to update time (NTP) and weather (HTTP).

During the WiFi sync, to avoid conflicts, ESP-NOW is temporarily de-initialized (esp_now_deinit()) and restarted immediately after WiFi is disconnected.

### Remote Sensor Logic (ESP-01S)
The ESP-01S is designed for maximum battery life.

Wake: On wake-up, it first checks its RTC (Real-Time Clock) memory for a sleepCounter.

Long Sleep Check: If sleepCounter > 0, it means the main unit commanded it to sleep. It decrements the counter, saves it back to RTC, and re-enters deep-sleep for 1 hour. This repeats until the counter hits 0. 

Normal Operation: If sleepCounter == 0, it proceeds with a normal wake cycle.

Measure: It takes 6 temperature readings from the DS18B20, performs a simple average/filter, and prepares the value. 

Send: It sends the temperature to the NodeMCU's MAC address via ESP-NOW. 

ACK Wait: It waits for an "ACK" (Acknowledgment) packet from the main unit. This ACK is sent by the main unit's onDataReceive callback.

Sleep (ACK OK): If the ACK is received, it sets its sleep time to 10 minutes (deep_sleep_base) and enters deep-sleep. 

Sleep (ACK Fail): If no ACK is received, it retries sending for 60 seconds. If it still fails, it assumes the main unit is offline and enters a shorter 6-minute deep-sleep as a fallback, saving battery while trying to reconnect sooner. 

### Main Controller "Continuous" Mode Logic
This is the most complex mode, relying on the ESP-01S.

Target: The goal is to reach a target temperature on the radiator (e.g., 55°C, var_menu.termo_temperature).

Control: The relay turns ON if the radiator temp (from ESP-NOW) is below its target AND the ambient room temp is below a comfort threshold (e.g., 18.7°C, var_global.soglia).

Safety Stop: If the ambient room temp exceeds a hard limit (e.g., 20.0°C, var_global.continous_limit), the relay shuts OFF regardless of the radiator temperature.

Fallback: If no data is received from the remote sensor (var_esp.esp_now_cont == 0), the system reverts to a basic thermostat mode based on the local sensor and the comfort threshold.

## Roadmap & Future Improvements
[ ] Fix the menu selection bug (scrolling from the last item to the first).

[ ] Implement the estimated_morning_temperature() function for smart pre-heating.

[ ] Refine the HTTP error handling and JSON parsing.

[ ] Improve the statistics and UI/UX of the display.
