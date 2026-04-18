#!/usr/bin/env python3
"""
Pi-side Mesh Bridge
Communicates with ESP32 mesh network via:
- Serial (UART) for ESP-NOW command passthrough
- BLE for direct device configuration
- WiFi for camera streaming (connects to hub AP)

The Pi acts as the controller — sends commands, receives sensor data,
and bridges the ESP32 mesh to the internet/cloud.
"""

import asyncio
import json
import sys
import time
import threading
import serial
from pathlib import Path

try:
    from bleak import BleakClient, BleakScanner
    BLE_AVAILABLE = True
except ImportError:
    BLE_AVAILABLE = False
    print("WARNING: bleak not installed — BLE disabled")

try:
    import requests
    HTTP_AVAILABLE = True
except ImportError:
    HTTP_AVAILABLE = False

# BLE UUIDs
SERVICE_UUID = "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
ROLE_CHAR = "beb5483e-36e1-4688-b7f5-ea07361b26a8"
NAME_CHAR = "beb5483e-36e1-4688-b7f5-ea07361b26a9"
CMD_CHAR = "beb5483e-36e1-4688-b7f5-ea07361b26aa"
STATUS_CHAR = "beb5483e-36e1-4688-b7f5-ea07361b26ab"
PEERS_CHAR = "beb5483e-36e1-4688-b7f5-ea07361b26ac"
ESPNOW_CMD_CHAR = "beb5483e-36e1-4688-b7f5-ea07361b26ad"


class SerialBridge:
    """Communicate with ESP32 hub via UART serial."""

    def __init__(self, port="/dev/serial0", baud=115200):
        self.port = port
        self.baud = baud
        self.ser = None
        self.running = False
        self.callbacks = []

    def connect(self):
        try:
            self.ser = serial.Serial(self.port, self.baud, timeout=1)
            self.running = True
            print(f"Serial connected: {self.port} @ {self.baud}")
            return True
        except Exception as e:
            print(f"Serial failed: {e}")
            return False

    def send_command(self, target, cmd):
        """Send command via ESP-NOW through the hub."""
        if self.ser:
            msg = f"CMD:{target}:{cmd}\n"
            self.ser.write(msg.encode())
            print(f"Serial TX: {msg.strip()}")

    def discover(self):
        if self.ser:
            self.ser.write(b"DISCOVER\n")

    def get_peers(self):
        if self.ser:
            self.ser.write(b"PEERS\n")

    def get_status(self):
        if self.ser:
            self.ser.write(b"STATUS\n")

    def on_message(self, callback):
        self.callbacks.append(callback)

    def listen(self):
        """Background listener for serial data from ESP32."""
        while self.running:
            try:
                if self.ser and self.ser.in_waiting:
                    line = self.ser.readline().decode().strip()
                    if line:
                        for cb in self.callbacks:
                            cb(line)
            except Exception as e:
                print(f"Serial read error: {e}")
                time.sleep(1)

    def start_listener(self):
        t = threading.Thread(target=self.listen, daemon=True)
        t.start()

    def close(self):
        self.running = False
        if self.ser:
            self.ser.close()


class BLEBridge:
    """Communicate with ESP32 devices via BLE."""

    @staticmethod
    async def scan(timeout=5.0):
        print("Scanning for ESP32 mesh devices...")
        devices = await BleakScanner.discover(timeout=timeout)
        esp_devices = [d for d in devices if d.name and "esp32" in d.name.lower()]
        for d in esp_devices:
            print(f"  Found: {d.name} ({d.address}) RSSI: {d.rssi}")
        return esp_devices

    @staticmethod
    async def configure_device(address, name=None, role=None):
        async with BleakClient(address) as client:
            print(f"Connected to {address}")
            if name:
                await client.write_gatt_char(NAME_CHAR, name.encode())
                print(f"Name set: {name}")
            if role is not None:
                await client.write_gatt_char(ROLE_CHAR, str(role).encode())
                print(f"Role set: {'HUB' if role == 0 else 'NODE'}")

    @staticmethod
    async def send_command(address, cmd):
        async with BleakClient(address) as client:
            await client.write_gatt_char(CMD_CHAR, cmd.encode())
            await asyncio.sleep(0.5)
            status = await client.read_gatt_char(STATUS_CHAR)
            print(f"Status: {status.decode()}")

    @staticmethod
    async def send_espnow_command(address, cmd):
        """Send ESP-NOW command through a connected ESP32."""
        async with BleakClient(address) as client:
            await client.write_gatt_char(ESPNOW_CMD_CHAR, cmd.encode())
            print(f"ESP-NOW command sent: {cmd}")

    @staticmethod
    async def get_status(address):
        async with BleakClient(address) as client:
            status = await client.read_gatt_char(STATUS_CHAR)
            return json.loads(status.decode())

    @staticmethod
    async def get_peers(address):
        async with BleakClient(address) as client:
            peers = await client.read_gatt_char(PEERS_CHAR)
            return json.loads(peers.decode())


class WiFiBridge:
    """Communicate with ESP32 hub via WiFi HTTP API."""

    def __init__(self, hub_ip="192.168.4.1"):
        self.hub_ip = hub_ip
        self.base_url = f"http://{hub_ip}"

    def get_status(self):
        try:
            r = requests.get(f"{self.base_url}/api/status", timeout=3)
            return r.json()
        except Exception as e:
            print(f"WiFi error: {e}")
            return None

    def get_peers(self):
        try:
            r = requests.get(f"{self.base_url}/api/peers", timeout=3)
            return r.json()
        except Exception as e:
            return []

    def send_command(self, target, cmd):
        try:
            r = requests.get(f"{self.base_url}/api/send",
                           params={"target": target, "cmd": cmd}, timeout=3)
            return r.json()
        except Exception as e:
            print(f"WiFi send error: {e}")
            return None

    def discover(self):
        try:
            r = requests.get(f"{self.base_url}/api/discover", timeout=3)
            return r.json()
        except Exception as e:
            return None


# ==================== CLI Interface ====================

def print_help():
    print("""
ESP32 Mesh Bridge — Commands:

  Serial:
    serial connect [port] [baud]   Connect to ESP32 via UART
    serial send <target> <cmd>     Send command via ESP-NOW
    serial discover                Discover mesh peers
    serial peers                   List known peers
    serial status                  Get hub status

  BLE:
    ble scan                       Scan for ESP32 devices
    ble config <addr> [name] [role] Configure device
    ble cmd <addr> <command>       Send BLE command
    ble espnow <addr> <command>    Send ESP-NOW via BLE
    ble status <addr>              Get device status
    ble peers <addr>               Get peer list

  WiFi:
    wifi status                    Get hub status via HTTP
    wifi peers                     List peers via HTTP
    wifi send <target> <cmd>       Send command via HTTP
    wifi discover                  Trigger discovery via HTTP

  Commands: UNLOCK, LOCK, BUZZER, FLASH_ON, FLASH_OFF, STATUS, PEERS
  Targets:  ALL (broadcast), or device name
""")


async def main():
    if len(sys.argv) < 2:
        print_help()
        return

    mode = sys.argv[1]

    if mode == "serial":
        action = sys.argv[2] if len(sys.argv) > 2 else "connect"
        bridge = SerialBridge(
            port=sys.argv[3] if len(sys.argv) > 3 else "/dev/serial0",
            baud=int(sys.argv[4]) if len(sys.argv) > 4 else 115200
        )

        if action == "connect":
            if bridge.connect():
                bridge.on_message(lambda msg: print(f"  RX: {msg}"))
                bridge.start_listener()
                print("Interactive mode. Type commands (CMD:target:command):")
                try:
                    while True:
                        line = input("> ")
                        if line == "quit":
                            break
                        elif line == "discover":
                            bridge.discover()
                        elif line == "peers":
                            bridge.get_peers()
                        elif line == "status":
                            bridge.get_status()
                        elif ":" in line:
                            parts = line.split(":", 1)
                            bridge.send_command(parts[0], parts[1])
                        else:
                            bridge.send_command("ALL", line)
                except KeyboardInterrupt:
                    pass
                bridge.close()
        elif action == "send":
            if bridge.connect():
                bridge.send_command(sys.argv[3], sys.argv[4])
                time.sleep(1)
                bridge.close()
        elif action == "discover":
            if bridge.connect():
                bridge.discover()
                time.sleep(3)
                bridge.close()

    elif mode == "ble":
        if not BLE_AVAILABLE:
            print("Install bleak: pip install bleak")
            return
        action = sys.argv[2] if len(sys.argv) > 2 else "scan"

        if action == "scan":
            await BLEBridge.scan()
        elif action == "config":
            addr = sys.argv[3]
            name = sys.argv[4] if len(sys.argv) > 4 else None
            role = int(sys.argv[5]) if len(sys.argv) > 5 else None
            await BLEBridge.configure_device(addr, name, role)
        elif action == "cmd":
            await BLEBridge.send_command(sys.argv[3], sys.argv[4])
        elif action == "espnow":
            await BLEBridge.send_espnow_command(sys.argv[3], sys.argv[4])
        elif action == "status":
            status = await BLEBridge.get_status(sys.argv[3])
            print(json.dumps(status, indent=2))
        elif action == "peers":
            peers = await BLEBridge.get_peers(sys.argv[3])
            print(json.dumps(peers, indent=2))

    elif mode == "wifi":
        action = sys.argv[2] if len(sys.argv) > 2 else "status"
        hub_ip = sys.argv[3] if len(sys.argv) > 3 and action != "send" else "192.168.4.1"
        bridge = WiFiBridge(hub_ip)

        if action == "status":
            status = bridge.get_status()
            if status:
                print(json.dumps(status, indent=2))
        elif action == "peers":
            peers = bridge.get_peers()
            print(json.dumps(peers, indent=2))
        elif action == "send":
            result = bridge.send_command(sys.argv[3], sys.argv[4])
            print(result)
        elif action == "discover":
            bridge.discover()

    else:
        print_help()


if __name__ == "__main__":
    asyncio.run(main())
