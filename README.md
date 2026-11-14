# 🌿 Smart AQI Monitoring System

An ESP32-based offline air-quality monitoring project with a browser dashboard, live sensor updates, trend charts, and CSV logging.

> **Project status:** Reported as successfully working on the hardware by the project owner. The source currently provided is a single Arduino `.ino` file with the web dashboard embedded in the firmware.

## Overview

The ESP32 creates its own Wi-Fi access point and hosts a local dashboard. Sensor readings are sent to the dashboard through a WebSocket connection. The firmware also stores periodic readings in a CSV file on the ESP32's LittleFS filesystem.

### Features present in the supplied source

- Offline Wi-Fi access point (`AQI_MONITOR`)
- Browser-based dashboard served by the ESP32
- Live updates over WebSocket
- Temperature, humidity, and pressure readings from BME680
- PM1.0, PM2.5, and PM10 readings from PMS5003
- MQ-135-derived gas readings labelled CO₂ and NH₃ in the dashboard
- AQI and IAQ values with category labels
- Live sparkline charts
- Dark/light dashboard theme toggle
- CSV logging to LittleFS, with download and clear endpoints
- Sensor connection indicators and free-heap display
- Sensor read interval: 3 seconds; CSV logging interval: 60 seconds

## Hardware

| Component | Purpose | ESP32 connection in source |
|---|---|---|
| ESP32 | Main controller and local web server | — |
| BME680 | Temperature, humidity, pressure, and gas-resistance readings | SDA: GPIO 21, SCL: GPIO 22 |
| MQ-135 | Analog gas-sensor input | Analog output: GPIO 34 |
| PMS5003 | Particulate matter sensor | ESP32 RX2: GPIO 16, ESP32 TX2: GPIO 17 |

**UART wiring note:** Connect PMS5003 TX to ESP32 GPIO 16 (RX2), and PMS5003 RX to ESP32 GPIO 17 (TX2), with a common ground. Confirm the sensor board's voltage and signal-level requirements before wiring.

## Software and libraries

The source includes these libraries:

- `WiFi.h` (ESP32 Arduino core)
- `Wire.h` (Arduino core)
- `AsyncTCP.h`
- `ESPAsyncWebServer.h`
- `Adafruit_BME680.h` (Adafruit BME680 library)
- `MQUnifiedsensor.h` (MQUnifiedsensor library)
- `LittleFS.h` (ESP32 Arduino core)

Install the ESP32 board support package and the external libraries using the library manager or their maintainers' installation instructions. The exact tested library and board-core versions were not included with the supplied source.

## Configure before uploading

1. Open `AQI_Second_Final_Code.ino` in Arduino IDE.
2. Select the correct ESP32 board and serial port.
3. Set your own Wi-Fi access-point password in the `AP_PASS` constant. The repository copy intentionally uses the placeholder `CHANGE_THIS_PASSWORD`.
4. Confirm the sensor wiring and required libraries.
5. Compile and upload the sketch.
6. Open the Serial Monitor at **115200 baud** and note the access-point IP printed by the ESP32.
7. Connect a phone, tablet, or computer to the `AQI_MONITOR` Wi-Fi network using the password you configured, then open the printed IP address in a browser.

## Dashboard and data endpoints

| Path | Method | Purpose |
|---|---|---|
| `/` | GET | Serves the dashboard |
| `/ws` | WebSocket | Streams live sensor data |
| `/download` | GET | Downloads the CSV log |
| `/clear` | POST | Clears the CSV log |
| `/refresh` | GET | Requests a sensor refresh |
| `/mem` | GET | Returns free and total heap information |

The CSV log is stored at `/data.csv` in LittleFS. The firmware writes a log row approximately once per minute and recreates the file when it grows beyond 100 KB.

## AQI and IAQ calculation note

The supplied firmware uses custom breakpoint tables for PM2.5, PM10, and the MQ-135-derived NH₃ value, then selects the highest sub-index as `final_aqi`. Its IAQ value is calculated using the BME680 gas-resistance/humidity logic, with a fallback based on the MQ-135-derived CO₂ value and PM2.5.

**These are the calculations implemented in this project source. They should not be represented as official CPCB AQI calculations or certified measurements without separate validation against the applicable standard and calibrated reference instruments.** MQ-series gas estimates are sensitive to calibration, warm-up, environmental conditions, and sensor cross-sensitivity.

## Repository structure

```text
Smart-AQI-Monitoring-System/
├── AQI_Second_Final_Code.ino
├── README.md
└── .gitignore
```

Wiring diagrams, build photographs, and dashboard screenshots can be added under an `assets/` directory when they are available.

## Limitations and validation

- The source provided for this repository is a single `.ino` file; no separate web assets, PCB files, calibration records, or test report were supplied with it.
- Sensor accuracy depends on the sensor modules, calibration, placement, warm-up, and operating conditions.
- Verify the AQI/IAQ formulas and sensor-derived gas values before using the readings for health, compliance, or official reporting.
- The code's exact board-core and library versions have not been documented yet.

## License

No license has been selected for this repository yet. Until a license is added, others do not automatically receive permission to reuse, modify, or distribute the code.

## Author

**Arpit Kumar Saini**  
GitHub: [@arpitsaini0407-jpg](https://github.com/arpitsaini0407-jpg)
