# ESP32-CAM Bridge

Dual-mode (WiFi + BLE) firmware for the Inland ESP32-CAM with a built-in web UI, plus a Raspberry Pi dashboard for remote viewing and control.

## Architecture

```
[ESP32-CAM] ---WiFi---> [Your Network] <---WiFi--- [Raspberry Pi]
     |                                                   |
     +--------BLE (config)-------------------------------+
     |
     +--- Web UI on :80 (controls, settings, capture)
     +--- MJPEG stream on :81/stream
```

## Quick Start

### 1. Wire Pi to ESP32-CAM (for flashing only)

| Pi Pin | Function | ESP32-CAM Pin |
|--------|----------|---------------|
| 2 | 5V | 5V |
| 6 | GND | GND |
| 8 | TX | U0R |
| 10 | RX | U0T |
| — | — | IO0 → GND (flash mode) |

### 2. Flash Firmware

```bash
# Install Arduino IDE with ESP32 board support
# Open firmware/esp32-cam-bridge/esp32-cam-bridge.ino
# Select Board: AI Thinker ESP32-CAM
# Export compiled binary, then:

esptool.py --port /dev/ttyS0 --baud 460800 write_flash -z 0x1000 firmware.bin
```

**Disconnect IO0 from GND after flashing and reset the board.**

### 3. Configure WiFi via BLE

```bash
cd pi && bash install.sh
source ~/esp32-cam-bridge/venv/bin/activate

# Scan for device
python3 ble_config.py scan

# Set WiFi credentials (saved to ESP32 flash)
python3 ble_config.py wifi "YourSSID" "YourPassword"
```

### 4. Access the Web UI

Once connected, the ESP32 serves its own web UI:
- **Web UI:** `http://<ESP32_IP>/`
- **Stream:** `http://<ESP32_IP>:81/stream`

Or use the Pi dashboard:
```bash
python3 dashboard.py
# Open http://<Pi_IP>:5000
```

## Features

### ESP32 Web UI
- Live MJPEG streaming with start/stop
- Single frame capture
- Resolution (QQVGA to UXGA)
- Quality, brightness, contrast, saturation sliders
- H-mirror, V-flip, flash LED toggles
- System stats (uptime, heap, PSRAM, RSSI)
- BLE connection status

### BLE Configuration
- Set WiFi SSID/password without reflashing
- Credentials persist in ESP32 flash (NVS)
- Commands: SAVE, CONNECT, RESTART, STATUS
- Auto-reconnects on WiFi drop

### Pi Dashboard
- Stream viewer proxied from ESP32
- Device info and status
- Link to full ESP32 web UI
- Auto-refreshing stats

## Files

```
firmware/
  esp32-cam-bridge/
    esp32-cam-bridge.ino   # Main Arduino sketch
    camera_pins.h          # Inland ESP32-CAM pin map
    web_ui.h               # Embedded HTML/CSS/JS
pi/
    install.sh             # Pi dependency installer
    ble_config.py          # BLE WiFi provisioning tool
    dashboard.py           # Flask dashboard
```

## Hardware

- **Inland ESP32-CAM** (AI-Thinker compatible, OV2640 sensor)
- **Raspberry Pi** (any model — Pi 4/5 recommended)
- Jumper wires (flashing only — wireless after that)
