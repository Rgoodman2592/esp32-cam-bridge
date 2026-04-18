#!/usr/bin/env python3
"""
BLE configuration tool for ESP32-CAM Bridge.
Send WiFi credentials and commands to the ESP32 over Bluetooth.
"""

import asyncio
import sys
from bleak import BleakClient, BleakScanner

SERVICE_UUID = "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
SSID_CHAR = "beb5483e-36e1-4688-b7f5-ea07361b26a8"
PASS_CHAR = "beb5483e-36e1-4688-b7f5-ea07361b26a9"
CMD_CHAR = "beb5483e-36e1-4688-b7f5-ea07361b26aa"
STATUS_CHAR = "beb5483e-36e1-4688-b7f5-ea07361b26ab"


async def scan():
    print("Scanning for ESP32-CAM Bridge devices...")
    devices = await BleakScanner.discover(timeout=5.0)
    esp_devices = [d for d in devices if d.name and "esp32-cam" in d.name.lower()]

    if not esp_devices:
        print("No ESP32-CAM devices found. Make sure the device is powered on.")
        return None

    for i, d in enumerate(esp_devices):
        print(f"  [{i}] {d.name} ({d.address}) RSSI: {d.rssi}")

    if len(esp_devices) == 1:
        return esp_devices[0].address

    idx = int(input("Select device: "))
    return esp_devices[idx].address


async def configure_wifi(address, ssid, password):
    async with BleakClient(address) as client:
        print(f"Connected to {address}")

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


async def send_command(address, cmd):
    async with BleakClient(address) as client:
        await client.write_gatt_char(CMD_CHAR, cmd.encode())
        await asyncio.sleep(0.5)
        status = await client.read_gatt_char(STATUS_CHAR)
        print(f"Status: {status.decode()}")


async def main():
    if len(sys.argv) < 2:
        print("Usage:")
        print("  python3 ble_config.py scan")
        print("  python3 ble_config.py wifi <SSID> <PASSWORD>")
        print("  python3 ble_config.py status")
        print("  python3 ble_config.py restart")
        return

    cmd = sys.argv[1]

    if cmd == "scan":
        await scan()
    elif cmd == "wifi":
        if len(sys.argv) < 4:
            print("Usage: python3 ble_config.py wifi <SSID> <PASSWORD>")
            return
        address = await scan()
        if address:
            await configure_wifi(address, sys.argv[2], sys.argv[3])
    elif cmd == "status":
        address = await scan()
        if address:
            await send_command(address, "STATUS")
    elif cmd == "restart":
        address = await scan()
        if address:
            await send_command(address, "RESTART")


if __name__ == "__main__":
    asyncio.run(main())
