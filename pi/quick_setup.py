#!/usr/bin/env python3
"""Quick WiFi setup for ESP32-CAM Bridge via BLE."""

import asyncio
import sys
from bleak import BleakClient, BleakScanner

SERVICE = "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
SSID_CHAR = "beb5483e-36e1-4688-b7f5-ea07361b26a8"
PASS_CHAR = "beb5483e-36e1-4688-b7f5-ea07361b26a9"
CMD_CHAR = "beb5483e-36e1-4688-b7f5-ea07361b26aa"
STATUS_CHAR = "beb5483e-36e1-4688-b7f5-ea07361b26ab"


async def main():
    if len(sys.argv) < 3:
        print("Usage: sudo python3 quick_setup.py <SSID> <PASSWORD>")
        return

    ssid = sys.argv[1]
    password = sys.argv[2]

    print("Scanning for ESP32-CAM devices...")
    devices = await BleakScanner.discover(timeout=5.0)
    esp_devices = [d for d in devices if d.name and "esp32" in d.name.lower()]

    if not esp_devices:
        print("No ESP32-CAM devices found. Make sure it is powered on.")
        return

    device = esp_devices[0]
    print(f"Found: {device.name} ({device.address})")

    async with BleakClient(device.address) as client:
        print(f"Connected to {device.address}")

        await client.write_gatt_char(SSID_CHAR, ssid.encode())
        print(f"SSID set: {ssid}")

        await client.write_gatt_char(PASS_CHAR, password.encode())
        print("Password set")

        await client.write_gatt_char(CMD_CHAR, b"SAVE")
        await asyncio.sleep(0.5)

        await client.write_gatt_char(CMD_CHAR, b"CONNECT")
        print("Connecting to WiFi...")

        for _ in range(15):
            await asyncio.sleep(1)
            status = await client.read_gatt_char(STATUS_CHAR)
            status_str = status.decode()
            print(f"  Status: {status_str}")

            if status_str.startswith("CONNECTED:"):
                ip = status_str.split(":")[1]
                print(f"\nConnected! Web UI: http://{ip}")
                print(f"Stream:   http://{ip}:81/stream")
                return

            if status_str == "FAILED":
                print("\nConnection failed. Check credentials.")
                return

        print("\nTimeout waiting for connection.")


if __name__ == "__main__":
    asyncio.run(main())
