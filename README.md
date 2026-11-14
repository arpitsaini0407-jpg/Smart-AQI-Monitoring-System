
# 🌿 Smart AQI Monitoring System

An ESP32-based IoT Smart Air Quality Monitoring System featuring offline monitoring, a real-time web dashboard, environmental sensors, live charts, and CSV data logging.

<p align="center">
  <img src="assets/dashboard-preview/live-dashboard-display.jpg" alt="Smart AQI Monitor Working Dashboard" width="850">
</p>

<p align="center">
  <b>Designed and Developed by Arpit Kumar Saini</b>
</p>

---

## 📌 Project Overview

The Smart AQI Monitoring System is an ESP32-based IoT project designed to monitor indoor environmental conditions and air quality.

The ESP32 creates its own Wi-Fi access point and hosts a local web dashboard. Users can connect through a phone, tablet, or computer without requiring an internet connection.

Sensor readings are transmitted using WebSocket communication, displayed through live charts, and periodically stored in the ESP32 LittleFS filesystem as CSV data.

## ✨ Key Features

- Offline Wi-Fi Access Point
- Real-time browser-based dashboard
- Live sensor updates using WebSockets
- Temperature, humidity and pressure monitoring
- PM1.0, PM2.5 and PM10 monitoring
- MQ-135 gas sensor readings
- AQI and IAQ indicators
- Live graphical trend charts
- Dark and Light dashboard themes
- CSV data logging
- Download and clear CSV records
- Sensor connection status indicators
- ESP32 memory monitoring

## 🖥️ Working Dashboard

The project dashboard displays environmental readings and graphical trends in a single interface.

![Working Dashboard](assets/dashboard-preview/live-dashboard-display.jpg)

## 🔧 Hardware Components

| Component | Purpose |
|---|---|
| ESP32 (30-pin) | Main controller and web server |
| BME680 | Temperature, humidity, pressure and gas resistance |
| PMS5003 | Particulate matter monitoring |
| MQ-135 | Gas sensing |
| 19-inch Monitor | Dashboard display |
| Android TV Box | Dashboard display platform |
| Power Supply | System power |

## 🔌 Circuit Diagram

The following diagram illustrates the system architecture and sensor connections.

![Smart AQI Circuit Diagram](assets/circuit-diagrams/system-wiring-diagram.png)

## 🏗️ Hardware Implementation

The complete system is assembled inside a custom enclosure with the monitor, controller, power components and sensor connections.

![Internal Hardware Setup](assets/hardware-setup/internal-hardware-setup.jpg)

## 📡 ESP32 Pin Configuration

| Sensor | ESP32 Pin |
|---|---|
| BME680 SDA | GPIO 21 |
| BME680 SCL | GPIO 22 |
| MQ-135 Analog Output | GPIO 34 |
| PMS5003 TX | GPIO 16 (RX2) |
| PMS5003 RX | GPIO 17 (TX2) |

## 💻 Software & Technologies

- Arduino IDE
- ESP32 Arduino Core
- Embedded C++
- HTML, CSS and JavaScript
- WebSocket Communication
- LittleFS File System
- Asynchronous Web Server

### Libraries Used

- WiFi.h
- Wire.h
- AsyncTCP.h
- ESPAsyncWebServer.h
- Adafruit_BME680.h
- MQUnifiedsensor.h
- LittleFS.h

## ⚙️ Installation & Setup

1. Clone or download this repository.
2. Open `AQI_Second_Final_Code.ino` in Arduino IDE.
3. Install ESP32 board support.
4. Install the required libraries.
5. Select your ESP32 board and serial port.
6. Configure your own Wi-Fi access-point password in `AP_PASS`.
7. Verify sensor wiring.
8. Compile and upload the firmware.
9. Open Serial Monitor at 115200 baud.
10. Connect your device to the `AQI_MONITOR` Wi-Fi network.
11. Open the IP address printed in Serial Monitor in your browser.

**Note:** Configure a secure Wi-Fi password before uploading the firmware.

## 📊 Dashboard Endpoints

| Endpoint | Function |
|---|---|
| `/` | Main dashboard |
| `/ws` | Live WebSocket data |
| `/download` | Download CSV data |
| `/clear` | Clear stored CSV data |
| `/refresh` | Refresh sensor readings |
| `/mem` | ESP32 memory information |

## 📁 Repository Structure

```text
Smart-AQI-Monitoring-System/
│
├── AQI_Second_Final_Code.ino
├── README.md
├── .gitignore
│
└── assets/
    ├── hardware-setup/
    │   └── internal-hardware-setup.jpg
    │
    ├── dashboard-preview/
    │   └── live-dashboard-display.jpg
    │
    └── circuit-diagrams/
        └── system-wiring-diagram.png
```

## ⚠️ Important Notes

- The system is designed for local offline monitoring.
- Sensor readings depend on calibration and environmental conditions.
- MQ-series gas readings are estimates and can be affected by cross-sensitivity.
- AQI/IAQ calculations in the firmware are project-specific and should not be treated as certified official measurements.
- Verify electrical connections and sensor voltage requirements before powering the system.
- Exact tested library versions have not yet been documented.

## 🚀 Future Improvements

- Enhanced sensor calibration
- Historical data visualization
- Improved dashboard interface
- Remote monitoring capabilities
- Automated environmental alerts
- Advanced data analytics

## 👨‍💻 Author

**Arpit Kumar Saini**

AI | Software | IoT Developer

- GitHub: [@arpitsaini0407-jpg](https://github.com/arpitsaini0407-jpg)
- LinkedIn: [Arpit Kumar Saini](https://www.linkedin.com/in/arpit-kumar-saini-4a4297447/)

---

<p align="center">
  <b>Building Intelligent Solutions with AI, Software & IoT.</b>
</p>
