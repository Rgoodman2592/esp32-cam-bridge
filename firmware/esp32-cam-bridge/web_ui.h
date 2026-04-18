#pragma once

const char WEB_UI_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>ESP32-CAM Bridge</title>
<style>
  * { margin: 0; padding: 0; box-sizing: border-box; }
  body { font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', sans-serif; background: #0a0a0a; color: #e0e0e0; }
  .header { background: #111; padding: 12px 20px; display: flex; justify-content: space-between; align-items: center; border-bottom: 1px solid #222; }
  .header h1 { font-size: 18px; font-weight: 600; color: #fff; }
  .status-dot { width: 10px; height: 10px; border-radius: 50%; display: inline-block; margin-right: 6px; }
  .status-dot.on { background: #22c55e; box-shadow: 0 0 6px #22c55e; }
  .status-dot.off { background: #ef4444; }
  .main { display: grid; grid-template-columns: 1fr 320px; gap: 0; height: calc(100vh - 49px); }
  .stream-panel { background: #000; display: flex; align-items: center; justify-content: center; position: relative; }
  .stream-panel img { max-width: 100%; max-height: 100%; object-fit: contain; }
  .stream-overlay { position: absolute; top: 12px; left: 12px; display: flex; gap: 8px; }
  .badge { background: rgba(0,0,0,0.7); padding: 4px 10px; border-radius: 4px; font-size: 12px; backdrop-filter: blur(4px); }
  .sidebar { background: #111; border-left: 1px solid #222; overflow-y: auto; padding: 16px; }
  .section { margin-bottom: 20px; }
  .section h3 { font-size: 13px; text-transform: uppercase; letter-spacing: 0.5px; color: #888; margin-bottom: 10px; }
  .control { margin-bottom: 12px; }
  .control label { display: block; font-size: 13px; color: #aaa; margin-bottom: 4px; }
  .control select, .control input[type=range] { width: 100%; background: #1a1a1a; border: 1px solid #333; color: #fff; padding: 6px 8px; border-radius: 4px; font-size: 13px; }
  .control input[type=range] { padding: 0; height: 6px; -webkit-appearance: none; appearance: none; border: none; border-radius: 3px; cursor: pointer; }
  .control input[type=range]::-webkit-slider-thumb { -webkit-appearance: none; width: 16px; height: 16px; border-radius: 50%; background: #3b82f6; cursor: pointer; }
  .range-val { float: right; font-size: 12px; color: #666; }
  .toggle-row { display: flex; justify-content: space-between; align-items: center; padding: 8px 0; border-bottom: 1px solid #1a1a1a; }
  .toggle-row label { font-size: 13px; color: #ccc; }
  .toggle { position: relative; width: 40px; height: 22px; }
  .toggle input { opacity: 0; width: 0; height: 0; }
  .toggle .slider { position: absolute; top: 0; left: 0; right: 0; bottom: 0; background: #333; border-radius: 11px; cursor: pointer; transition: 0.2s; }
  .toggle .slider:before { content: ''; position: absolute; width: 16px; height: 16px; left: 3px; bottom: 3px; background: #888; border-radius: 50%; transition: 0.2s; }
  .toggle input:checked + .slider { background: #3b82f6; }
  .toggle input:checked + .slider:before { transform: translateX(18px); background: #fff; }
  .btn { display: inline-flex; align-items: center; gap: 6px; padding: 8px 16px; border: none; border-radius: 6px; font-size: 13px; font-weight: 500; cursor: pointer; transition: 0.15s; width: 100%; justify-content: center; }
  .btn-primary { background: #3b82f6; color: #fff; }
  .btn-primary:hover { background: #2563eb; }
  .btn-danger { background: #1a1a1a; color: #ef4444; border: 1px solid #333; }
  .btn-danger:hover { background: #1c1c1c; border-color: #ef4444; }
  .btn-outline { background: #1a1a1a; color: #ccc; border: 1px solid #333; }
  .btn-outline:hover { background: #222; border-color: #555; }
  .stats { display: grid; grid-template-columns: 1fr 1fr; gap: 8px; }
  .stat { background: #1a1a1a; padding: 10px; border-radius: 6px; }
  .stat .val { font-size: 18px; font-weight: 600; color: #fff; }
  .stat .lbl { font-size: 11px; color: #666; margin-top: 2px; }
  .btn-group { display: flex; flex-direction: column; gap: 8px; }
  @media (max-width: 768px) {
    .main { grid-template-columns: 1fr; grid-template-rows: 50vh 1fr; }
  }
</style>
</head>
<body>

<div class="header">
  <h1>ESP32-CAM Bridge</h1>
  <div id="conn-status"><span class="status-dot on"></span><span id="ip-display">Loading...</span></div>
</div>

<div class="main">
  <div class="stream-panel">
    <img id="stream" src="" alt="Camera stream">
    <div class="stream-overlay">
      <span class="badge" id="res-badge">VGA</span>
      <span class="badge" id="clients-badge">0 viewers</span>
    </div>
  </div>

  <div class="sidebar">
    <div class="section">
      <h3>Live View</h3>
      <div class="btn-group">
        <button class="btn btn-primary" onclick="toggleStream()">Start Stream</button>
        <button class="btn btn-outline" onclick="capturePhoto()">Capture Photo</button>
      </div>
    </div>

    <div class="section">
      <h3>Image Settings</h3>
      <div class="control">
        <label>Resolution</label>
        <select id="framesize" onchange="setSetting('framesize', this.value)">
          <option value="0">QQVGA (160x120)</option>
          <option value="3">HQVGA (240x176)</option>
          <option value="5">QVGA (320x240)</option>
          <option value="6">CIF (400x296)</option>
          <option value="8" selected>VGA (640x480)</option>
          <option value="9">SVGA (800x600)</option>
          <option value="10">XGA (1024x768)</option>
          <option value="11">HD (1280x720)</option>
          <option value="12">SXGA (1280x1024)</option>
          <option value="13">UXGA (1600x1200)</option>
        </select>
      </div>
      <div class="control">
        <label>Quality <span class="range-val" id="quality-val">12</span></label>
        <input type="range" id="quality" min="4" max="63" value="12" onchange="setSetting('quality', this.value); document.getElementById('quality-val').textContent=this.value">
      </div>
      <div class="control">
        <label>Brightness <span class="range-val" id="brightness-val">0</span></label>
        <input type="range" id="brightness" min="-2" max="2" value="0" onchange="setSetting('brightness', this.value); document.getElementById('brightness-val').textContent=this.value">
      </div>
      <div class="control">
        <label>Contrast <span class="range-val" id="contrast-val">0</span></label>
        <input type="range" id="contrast" min="-2" max="2" value="0" onchange="setSetting('contrast', this.value); document.getElementById('contrast-val').textContent=this.value">
      </div>
      <div class="control">
        <label>Saturation <span class="range-val" id="saturation-val">0</span></label>
        <input type="range" id="saturation" min="-2" max="2" value="0" onchange="setSetting('saturation', this.value); document.getElementById('saturation-val').textContent=this.value">
      </div>
    </div>

    <div class="section">
      <h3>Toggles</h3>
      <div class="toggle-row">
        <label>H-Mirror</label>
        <label class="toggle"><input type="checkbox" id="hmirror" onchange="setSetting('hmirror', this.checked?1:0)"><span class="slider"></span></label>
      </div>
      <div class="toggle-row">
        <label>V-Flip</label>
        <label class="toggle"><input type="checkbox" id="vflip" onchange="setSetting('vflip', this.checked?1:0)"><span class="slider"></span></label>
      </div>
      <div class="toggle-row">
        <label>Flash LED</label>
        <label class="toggle"><input type="checkbox" id="flash" onchange="setSetting('flash', this.checked?1:0)"><span class="slider"></span></label>
      </div>
    </div>

    <div class="section">
      <h3>System</h3>
      <div class="stats">
        <div class="stat"><div class="val" id="uptime">0s</div><div class="lbl">Uptime</div></div>
        <div class="stat"><div class="val" id="heap">0</div><div class="lbl">Free Heap</div></div>
        <div class="stat"><div class="val" id="rssi">0</div><div class="lbl">WiFi RSSI</div></div>
        <div class="stat"><div class="val" id="psram">0</div><div class="lbl">Free PSRAM</div></div>
      </div>
    </div>

    <div class="section">
      <h3>Connectivity</h3>
      <div class="stats" style="grid-template-columns:1fr">
        <div class="stat"><div class="val" id="ble-status">--</div><div class="lbl">BLE Status</div></div>
      </div>
    </div>

    <div class="section">
      <button class="btn btn-danger" onclick="if(confirm('Restart ESP32?')) fetch('/settings?restart=1')">Restart Device</button>
    </div>
  </div>
</div>

<script>
let streaming = false;

function toggleStream() {
  const img = document.getElementById('stream');
  const btn = event.target;
  if (streaming) {
    img.src = '';
    btn.textContent = 'Start Stream';
    streaming = false;
  } else {
    img.src = 'http://' + location.hostname + ':81/stream';
    btn.textContent = 'Stop Stream';
    streaming = true;
  }
}

function capturePhoto() {
  window.open('/capture', '_blank');
}

function setSetting(key, val) {
  fetch('/settings?' + key + '=' + val).catch(e => console.error(e));
}

function formatUptime(s) {
  if (s < 60) return s + 's';
  if (s < 3600) return Math.floor(s/60) + 'm';
  if (s < 86400) return Math.floor(s/3600) + 'h ' + Math.floor((s%3600)/60) + 'm';
  return Math.floor(s/86400) + 'd ' + Math.floor((s%86400)/3600) + 'h';
}

function formatBytes(b) {
  if (b > 1048576) return (b / 1048576).toFixed(1) + 'MB';
  if (b > 1024) return (b / 1024).toFixed(0) + 'KB';
  return b + 'B';
}

const resNames = {0:'QQVGA',3:'HQVGA',5:'QVGA',6:'CIF',8:'VGA',9:'SVGA',10:'XGA',11:'HD',12:'SXGA',13:'UXGA'};

function refreshStatus() {
  fetch('/status').then(r => r.json()).then(d => {
    document.getElementById('ip-display').textContent = d.ip;
    document.getElementById('uptime').textContent = formatUptime(d.uptime);
    document.getElementById('heap').textContent = formatBytes(d.freeHeap);
    document.getElementById('rssi').textContent = d.rssi + 'dBm';
    document.getElementById('psram').textContent = formatBytes(d.psram);
    document.getElementById('clients-badge').textContent = d.streamClients + ' viewer' + (d.streamClients !== 1 ? 's' : '');
    document.getElementById('res-badge').textContent = resNames[d.frameSize] || d.frameSize;
    document.getElementById('ble-status').textContent = d.bleConnected ? 'Connected' : (d.ble ? 'Advertising' : 'Off');
    document.getElementById('framesize').value = d.frameSize;
    document.getElementById('quality').value = d.quality;
    document.getElementById('quality-val').textContent = d.quality;
    document.getElementById('brightness').value = d.brightness;
    document.getElementById('brightness-val').textContent = d.brightness;
    document.getElementById('contrast').value = d.contrast;
    document.getElementById('contrast-val').textContent = d.contrast;
    document.getElementById('saturation').value = d.saturation;
    document.getElementById('saturation-val').textContent = d.saturation;
    document.getElementById('hmirror').checked = d.hmirror;
    document.getElementById('vflip').checked = d.vflip;
    document.getElementById('flash').checked = d.flash;
  }).catch(() => {
    document.getElementById('ip-display').textContent = 'Disconnected';
    document.querySelector('.status-dot').className = 'status-dot off';
  });
}

refreshStatus();
setInterval(refreshStatus, 3000);
</script>
</body>
</html>
)rawliteral";
