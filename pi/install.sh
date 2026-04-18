#!/bin/bash
# Install dependencies on the Raspberry Pi for ESP32-CAM Bridge
set -e

echo "=== ESP32-CAM Bridge - Pi Setup ==="

sudo apt-get update
sudo apt-get install -y python3-pip python3-venv bluetooth bluez

mkdir -p ~/esp32-cam-bridge
cd ~/esp32-cam-bridge
python3 -m venv venv
source venv/bin/activate

pip install flask requests bleak esptool

echo ""
echo "=== Setup complete ==="
echo "Flash firmware:  esptool.py --port /dev/ttyS0 --baud 460800 write_flash -z 0x1000 firmware.bin"
echo "BLE config:      python3 ble_config.py"
echo "Dashboard:       python3 dashboard.py"
