#!/usr/bin/env python3
"""
Pi-side dashboard for ESP32-CAM Bridge.
Proxies the ESP32 web UI and adds Pi-level controls.
"""

import os
import requests
from flask import Flask, render_template_string, request, jsonify

app = Flask(__name__)

ESP32_IP = os.environ.get("ESP32_IP", "")

DASHBOARD_HTML = """
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>ESP32-CAM Dashboard</title>
<style>
  * { margin: 0; padding: 0; box-sizing: border-box; }
  body { font-family: -apple-system, sans-serif; background: #0a0a0a; color: #e0e0e0; padding: 20px; }
  h1 { font-size: 22px; margin-bottom: 20px; }
  .grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(400px, 1fr)); gap: 16px; }
  .card { background: #111; border: 1px solid #222; border-radius: 8px; overflow: hidden; }
  .card-header { padding: 12px 16px; border-bottom: 1px solid #222; font-weight: 600; display: flex; justify-content: space-between; align-items: center; }
  .card-body { padding: 16px; }
  .stream-container { background: #000; aspect-ratio: 4/3; display: flex; align-items: center; justify-content: center; }
  .stream-container img { width: 100%; height: 100%; object-fit: contain; }
  .status-dot { width: 8px; height: 8px; border-radius: 50%; display: inline-block; }
  .status-dot.on { background: #22c55e; }
  .status-dot.off { background: #ef4444; }
  input[type=text] { width: 100%; background: #1a1a1a; border: 1px solid #333; color: #fff; padding: 8px 12px; border-radius: 4px; font-size: 14px; margin-bottom: 8px; }
  .btn { padding: 8px 16px; border: none; border-radius: 6px; cursor: pointer; font-size: 13px; font-weight: 500; }
  .btn-blue { background: #3b82f6; color: #fff; }
  .btn-blue:hover { background: #2563eb; }
  .info-row { display: flex; justify-content: space-between; padding: 6px 0; border-bottom: 1px solid #1a1a1a; font-size: 13px; }
  .info-row:last-child { border: none; }
  .info-label { color: #888; }
  a { color: #3b82f6; text-decoration: none; }
  a:hover { text-decoration: underline; }
</style>
</head>
<body>

<h1>ESP32-CAM Dashboard</h1>

<div style="margin-bottom: 16px;">
  <label style="font-size:13px;color:#888;">ESP32 IP Address</label>
  <div style="display:flex;gap:8px;">
    <input type="text" id="esp-ip" placeholder="e.g. 192.168.1.100" value="{{ esp_ip }}">
    <button class="btn btn-blue" onclick="setIP()">Connect</button>
  </div>
</div>

<div class="grid">
  <div class="card">
    <div class="card-header">
      Live Stream
      <span><span class="status-dot" id="stream-dot"></span> <span id="stream-label">Offline</span></span>
    </div>
    <div class="stream-container">
      <img id="stream-img" src="" alt="No stream">
    </div>
    <div class="card-body" style="display:flex;gap:8px;">
      <button class="btn btn-blue" onclick="startStream()">Start</button>
      <button class="btn btn-blue" style="background:#333" onclick="stopStream()">Stop</button>
      <button class="btn btn-blue" style="background:#333" onclick="openESP()">Open ESP32 UI</button>
    </div>
  </div>

  <div class="card">
    <div class="card-header">Device Info</div>
    <div class="card-body" id="device-info">
      <div class="info-row"><span class="info-label">Status</span><span>Not connected</span></div>
    </div>
  </div>
</div>

<script>
let espIP = '{{ esp_ip }}';

function setIP() {
  espIP = document.getElementById('esp-ip').value.trim();
  fetch('/api/set-ip?ip=' + espIP).then(() => refreshInfo());
}

function startStream() {
  if (!espIP) { alert('Set ESP32 IP first'); return; }
  document.getElementById('stream-img').src = 'http://' + espIP + ':81/stream';
  document.getElementById('stream-dot').className = 'status-dot on';
  document.getElementById('stream-label').textContent = 'Live';
}

function stopStream() {
  document.getElementById('stream-img').src = '';
  document.getElementById('stream-dot').className = 'status-dot off';
  document.getElementById('stream-label').textContent = 'Offline';
}

function openESP() {
  if (espIP) window.open('http://' + espIP, '_blank');
}

function refreshInfo() {
  if (!espIP) return;
  fetch('http://' + espIP + '/status')
    .then(r => r.json())
    .then(d => {
      const info = document.getElementById('device-info');
      info.innerHTML = `
        <div class="info-row"><span class="info-label">IP</span><span>${d.ip}</span></div>
        <div class="info-row"><span class="info-label">WiFi RSSI</span><span>${d.rssi} dBm</span></div>
        <div class="info-row"><span class="info-label">Uptime</span><span>${Math.floor(d.uptime/60)}m ${d.uptime%60}s</span></div>
        <div class="info-row"><span class="info-label">Free Heap</span><span>${(d.freeHeap/1024).toFixed(0)} KB</span></div>
        <div class="info-row"><span class="info-label">PSRAM</span><span>${(d.psram/1024).toFixed(0)} KB</span></div>
        <div class="info-row"><span class="info-label">BLE</span><span>${d.bleConnected ? 'Connected' : d.ble ? 'Advertising' : 'Off'}</span></div>
        <div class="info-row"><span class="info-label">Stream Clients</span><span>${d.streamClients}</span></div>
        <div class="info-row"><span class="info-label">Full UI</span><span><a href="http://${d.ip}" target="_blank">Open</a></span></div>
      `;
    })
    .catch(() => {
      document.getElementById('device-info').innerHTML = '<div class="info-row"><span class="info-label">Status</span><span style="color:#ef4444">Cannot reach ESP32</span></div>';
    });
}

if (espIP) { refreshInfo(); setInterval(refreshInfo, 5000); }
</script>
</body>
</html>
"""


@app.route("/")
def index():
    return render_template_string(DASHBOARD_HTML, esp_ip=ESP32_IP)


@app.route("/api/set-ip")
def set_ip():
    global ESP32_IP
    ESP32_IP = request.args.get("ip", "")
    return jsonify({"ok": True, "ip": ESP32_IP})


@app.route("/api/status")
def proxy_status():
    if not ESP32_IP:
        return jsonify({"error": "No ESP32 IP set"}), 400
    try:
        r = requests.get(f"http://{ESP32_IP}/status", timeout=3)
        return jsonify(r.json())
    except Exception as e:
        return jsonify({"error": str(e)}), 502


@app.route("/api/settings")
def proxy_settings():
    if not ESP32_IP:
        return jsonify({"error": "No ESP32 IP set"}), 400
    try:
        r = requests.get(f"http://{ESP32_IP}/settings", params=request.args, timeout=3)
        return jsonify(r.json())
    except Exception as e:
        return jsonify({"error": str(e)}), 502


if __name__ == "__main__":
    print("ESP32-CAM Dashboard")
    print("Open http://localhost:5000")
    if ESP32_IP:
        print(f"ESP32 IP: {ESP32_IP}")
    else:
        print("Set ESP32 IP in the dashboard or: ESP32_IP=x.x.x.x python3 dashboard.py")
    app.run(host="0.0.0.0", port=5000)
