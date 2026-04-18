/*
 * ESP32 Multi-Protocol Mesh Firmware
 * Supports: ESP-NOW + WiFi AP + BLE simultaneously
 * Roles: HUB (central) or NODE (remote)
 *
 * ESP-NOW: peer-to-peer commands, sensor data, no router needed
 * WiFi:   camera streaming, web UI, HTTP API
 * BLE:    configuration, status, small data packets
 */

#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <WebServer.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <Preferences.h>
#include "config.h"

// ==================== Role Selection ====================
// Set via BLE or hardcode here
// ROLE_HUB: creates WiFi AP, coordinates nodes, bridges to Pi
// ROLE_NODE: connects to hub, sends sensor/camera data
#define ROLE_HUB  0
#define ROLE_NODE 1

int deviceRole = ROLE_NODE;  // Default: node (change via BLE)
String deviceName = "esp32-node-1";

// ==================== ESP-NOW ====================

#define MAX_PEERS 20
#define MSG_DISCOVERY  0x01
#define MSG_COMMAND    0x02
#define MSG_SENSOR     0x03
#define MSG_STATUS     0x04
#define MSG_CAMERA     0x05
#define MSG_ACK        0x06
#define MSG_RELAY      0x07
#define MSG_PING       0x08

typedef struct {
  uint8_t type;
  uint8_t sender[6];
  char name[16];
  uint8_t role;
  uint16_t payload_len;
  uint8_t payload[200];
} mesh_msg_t;

typedef struct {
  uint8_t mac[6];
  char name[16];
  uint8_t role;
  unsigned long lastSeen;
  bool active;
} peer_info_t;

peer_info_t peers[MAX_PEERS];
int peerCount = 0;
uint8_t broadcastMAC[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
uint8_t myMAC[6];

// Callback buffers
mesh_msg_t lastReceived;
bool newMessage = false;

// ==================== WiFi ====================

WebServer webServer(80);
WebServer streamServer(81);
bool wifiAPActive = false;
String apSSID = "ESP32-Mesh";
String apPass = "intrlocknet";

// ==================== BLE ====================

#define SERVICE_UUID           "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define ROLE_CHAR_UUID         "beb5483e-36e1-4688-b7f5-ea07361b26a8"
#define NAME_CHAR_UUID         "beb5483e-36e1-4688-b7f5-ea07361b26a9"
#define CMD_CHAR_UUID          "beb5483e-36e1-4688-b7f5-ea07361b26aa"
#define STATUS_CHAR_UUID       "beb5483e-36e1-4688-b7f5-ea07361b26ab"
#define PEERS_CHAR_UUID        "beb5483e-36e1-4688-b7f5-ea07361b26ac"
#define ESPNOW_CMD_CHAR_UUID   "beb5483e-36e1-4688-b7f5-ea07361b26ad"

BLECharacteristic *pStatusChar;
BLECharacteristic *pPeersChar;
bool bleConnected = false;

Preferences prefs;

// ==================== ESP-NOW Callbacks ====================

void onDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  // Send callback — can track delivery success
}

void onDataRecv(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
  if (len > sizeof(mesh_msg_t)) return;
  memcpy(&lastReceived, data, len);
  newMessage = true;
}

void getMACAddress() {
  WiFi.macAddress(myMAC);
}

String macToString(const uint8_t *mac) {
  char buf[18];
  snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  return String(buf);
}

// ==================== ESP-NOW Functions ====================

void initESPNOW() {
  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW init failed");
    return;
  }
  esp_now_register_send_cb(onDataSent);
  esp_now_register_recv_cb(onDataRecv);

  // Add broadcast peer
  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, broadcastMAC, 6);
  peerInfo.channel = 1;
  peerInfo.encrypt = false;
  esp_now_add_peer(&peerInfo);

  Serial.println("ESP-NOW initialized");
}

void sendMessage(const uint8_t *dest, uint8_t type, const char *payload, int len) {
  mesh_msg_t msg = {};
  msg.type = type;
  memcpy(msg.sender, myMAC, 6);
  strncpy(msg.name, deviceName.c_str(), 15);
  msg.role = deviceRole;
  msg.payload_len = min(len, 200);
  if (payload && len > 0) {
    memcpy(msg.payload, payload, msg.payload_len);
  }
  esp_now_send(dest, (uint8_t *)&msg, sizeof(mesh_msg_t));
}

void broadcast(uint8_t type, const char *payload, int len) {
  sendMessage(broadcastMAC, type, payload, len);
}

void sendDiscovery() {
  broadcast(MSG_DISCOVERY, NULL, 0);
}

void sendCommand(const uint8_t *dest, const char *cmd) {
  sendMessage(dest, MSG_COMMAND, cmd, strlen(cmd));
}

void sendSensorData(const char *data, int len) {
  broadcast(MSG_SENSOR, data, len);
}

void addOrUpdatePeer(const uint8_t *mac, const char *name, uint8_t role) {
  // Check if peer already exists
  for (int i = 0; i < peerCount; i++) {
    if (memcmp(peers[i].mac, mac, 6) == 0) {
      strncpy(peers[i].name, name, 15);
      peers[i].role = role;
      peers[i].lastSeen = millis();
      peers[i].active = true;
      return;
    }
  }

  // Add new peer
  if (peerCount < MAX_PEERS) {
    memcpy(peers[peerCount].mac, mac, 6);
    strncpy(peers[peerCount].name, name, 15);
    peers[peerCount].role = role;
    peers[peerCount].lastSeen = millis();
    peers[peerCount].active = true;

    // Register with ESP-NOW
    esp_now_peer_info_t peerInfo = {};
    memcpy(peerInfo.peer_addr, mac, 6);
    peerInfo.channel = 1;
    peerInfo.encrypt = false;
    if (!esp_now_is_peer_exist(mac)) {
      esp_now_add_peer(&peerInfo);
    }

    peerCount++;
    Serial.printf("New peer: %s (%s) role=%d\n", name, macToString(mac).c_str(), role);
  }
}

void processMessage() {
  if (!newMessage) return;
  newMessage = false;

  // Don't process our own messages
  if (memcmp(lastReceived.sender, myMAC, 6) == 0) return;

  // Update peer list
  addOrUpdatePeer(lastReceived.sender, lastReceived.name, lastReceived.role);

  switch (lastReceived.type) {
    case MSG_DISCOVERY: {
      Serial.printf("Discovery from %s\n", lastReceived.name);
      // Reply with our info
      sendMessage(lastReceived.sender, MSG_DISCOVERY, NULL, 0);
      break;
    }
    case MSG_COMMAND: {
      String cmd = String((char *)lastReceived.payload).substring(0, lastReceived.payload_len);
      Serial.printf("Command from %s: %s\n", lastReceived.name, cmd.c_str());
      handleCommand(cmd, lastReceived.sender);
      break;
    }
    case MSG_SENSOR: {
      Serial.printf("Sensor from %s: %.*s\n", lastReceived.name,
                    lastReceived.payload_len, lastReceived.payload);
      // Hub: forward to Pi via serial
      if (deviceRole == ROLE_HUB) {
        Serial.printf("SENSOR|%s|%.*s\n", lastReceived.name,
                      lastReceived.payload_len, lastReceived.payload);
      }
      break;
    }
    case MSG_STATUS: {
      Serial.printf("Status from %s: %.*s\n", lastReceived.name,
                    lastReceived.payload_len, lastReceived.payload);
      break;
    }
    case MSG_PING: {
      sendMessage(lastReceived.sender, MSG_ACK, "PONG", 4);
      break;
    }
    case MSG_RELAY: {
      // Hub relays commands between nodes
      if (deviceRole == ROLE_HUB && lastReceived.payload_len > 6) {
        uint8_t destMAC[6];
        memcpy(destMAC, lastReceived.payload, 6);
        sendMessage(destMAC, MSG_COMMAND,
                    (char *)(lastReceived.payload + 6),
                    lastReceived.payload_len - 6);
      }
      break;
    }
  }
}

void handleCommand(String cmd, const uint8_t *from) {
  if (cmd == "UNLOCK") {
    Serial.println("CMD: UNLOCK");
    // Trigger relay/GPIO here
    sendMessage(from, MSG_ACK, "UNLOCKED", 8);
  } else if (cmd == "LOCK") {
    Serial.println("CMD: LOCK");
    sendMessage(from, MSG_ACK, "LOCKED", 6);
  } else if (cmd == "BUZZER") {
    Serial.println("CMD: BUZZER");
    sendMessage(from, MSG_ACK, "BUZZED", 6);
  } else if (cmd == "FLASH_ON") {
    digitalWrite(4, HIGH);
    sendMessage(from, MSG_ACK, "FLASH_ON", 8);
  } else if (cmd == "FLASH_OFF") {
    digitalWrite(4, LOW);
    sendMessage(from, MSG_ACK, "FLASH_OFF", 9);
  } else if (cmd == "STATUS") {
    String status = "OK|" + String(millis()/1000) + "s|heap=" + String(ESP.getFreeHeap());
    sendMessage(from, MSG_STATUS, status.c_str(), status.length());
  } else if (cmd == "PEERS") {
    String peerList = getPeerListJSON();
    sendMessage(from, MSG_STATUS, peerList.c_str(), peerList.length());
  }
}

// ==================== WiFi AP (Hub Only) ====================

void initWiFiAP() {
  WiFi.mode(WIFI_AP_STA);  // AP + STA for ESP-NOW
  WiFi.softAP(apSSID.c_str(), apPass.c_str(), 1, 0, 4);
  wifiAPActive = true;

  Serial.printf("WiFi AP: %s (IP: %s)\n", apSSID.c_str(),
                WiFi.softAPIP().toString().c_str());
}

void initWiFiSTA() {
  WiFi.mode(WIFI_STA);
  // Set channel to match hub
  esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);
}

void setupWebServer() {
  webServer.on("/", []() {
    webServer.send(200, "text/html", getWebUI());
  });

  webServer.on("/api/status", []() {
    webServer.send(200, "application/json", getStatusJSON());
  });

  webServer.on("/api/peers", []() {
    webServer.send(200, "application/json", getPeerListJSON());
  });

  webServer.on("/api/send", []() {
    String target = webServer.arg("target");
    String cmd = webServer.arg("cmd");

    if (target == "broadcast") {
      broadcast(MSG_COMMAND, cmd.c_str(), cmd.length());
    } else {
      // Find peer by name
      for (int i = 0; i < peerCount; i++) {
        if (String(peers[i].name) == target) {
          sendCommand(peers[i].mac, cmd.c_str());
          break;
        }
      }
    }
    webServer.send(200, "application/json", "{\"sent\":true}");
  });

  webServer.on("/api/discover", []() {
    sendDiscovery();
    webServer.send(200, "application/json", "{\"discovering\":true}");
  });

  webServer.begin();
  Serial.println("Web server started on :80");
}

String getStatusJSON() {
  String json = "{";
  json += "\"name\":\"" + deviceName + "\",";
  json += "\"role\":" + String(deviceRole) + ",";
  json += "\"mac\":\"" + macToString(myMAC) + "\",";
  json += "\"uptime\":" + String(millis() / 1000) + ",";
  json += "\"freeHeap\":" + String(ESP.getFreeHeap()) + ",";
  json += "\"peerCount\":" + String(peerCount) + ",";
  json += "\"wifiAP\":" + String(wifiAPActive ? "true" : "false") + ",";
  json += "\"ble\":" + String(bleConnected ? "true" : "false");
  json += "}";
  return json;
}

String getPeerListJSON() {
  String json = "[";
  for (int i = 0; i < peerCount; i++) {
    if (i > 0) json += ",";
    json += "{";
    json += "\"name\":\"" + String(peers[i].name) + "\",";
    json += "\"mac\":\"" + macToString(peers[i].mac) + "\",";
    json += "\"role\":" + String(peers[i].role) + ",";
    json += "\"active\":" + String(peers[i].active ? "true" : "false") + ",";
    json += "\"lastSeen\":" + String((millis() - peers[i].lastSeen) / 1000);
    json += "}";
  }
  json += "]";
  return json;
}

String getWebUI() {
  return R"rawliteral(
<!DOCTYPE html><html><head>
<meta charset="UTF-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>ESP32 Mesh</title>
<style>
*{margin:0;padding:0;box-sizing:border-box}
body{font-family:-apple-system,sans-serif;background:#0a0a0a;color:#e0e0e0;padding:16px}
h1{font-size:20px;margin-bottom:16px}
.card{background:#111;border:1px solid #222;border-radius:8px;padding:16px;margin-bottom:12px}
.card h3{font-size:13px;text-transform:uppercase;color:#888;margin-bottom:8px}
.peer{display:flex;justify-content:space-between;padding:8px;border-bottom:1px solid #1a1a1a;font-size:13px}
.peer:last-child{border:none}
.dot{width:8px;height:8px;border-radius:50%;display:inline-block;margin-right:6px}
.on{background:#22c55e}
.off{background:#ef4444}
btn,.btn{padding:8px 16px;border:none;border-radius:6px;cursor:pointer;font-size:13px;background:#3b82f6;color:#fff;margin:2px}
.btn:hover{background:#2563eb}
.btn-sm{padding:4px 10px;font-size:12px}
input,select{background:#1a1a1a;border:1px solid #333;color:#fff;padding:6px 10px;border-radius:4px;font-size:13px;margin:2px}
.grid{display:grid;grid-template-columns:1fr 1fr;gap:8px}
.stat{background:#1a1a1a;padding:10px;border-radius:6px}
.stat .v{font-size:18px;font-weight:600;color:#fff}
.stat .l{font-size:11px;color:#666;margin-top:2px}
</style></head><body>
<h1>ESP32 Mesh Network</h1>
<div class="card"><h3>Device</h3>
<div class="grid">
<div class="stat"><div class="v" id="name">--</div><div class="l">Name</div></div>
<div class="stat"><div class="v" id="role">--</div><div class="l">Role</div></div>
<div class="stat"><div class="v" id="uptime">--</div><div class="l">Uptime</div></div>
<div class="stat"><div class="v" id="peers-count">0</div><div class="l">Peers</div></div>
</div></div>
<div class="card"><h3>Send Command</h3>
<select id="target"><option value="broadcast">All Devices</option></select>
<select id="cmd">
<option value="UNLOCK">Unlock</option><option value="LOCK">Lock</option>
<option value="BUZZER">Buzzer</option><option value="FLASH_ON">Flash On</option>
<option value="FLASH_OFF">Flash Off</option><option value="STATUS">Status</option>
<option value="PEERS">List Peers</option>
</select>
<button class="btn" onclick="send()">Send</button>
<button class="btn" onclick="discover()" style="background:#333">Discover</button>
</div>
<div class="card"><h3>Peers</h3><div id="peer-list">No peers discovered</div></div>
<script>
function refresh(){fetch('/api/status').then(r=>r.json()).then(d=>{
document.getElementById('name').textContent=d.name;
document.getElementById('role').textContent=d.role==0?'HUB':'NODE';
document.getElementById('uptime').textContent=d.uptime+'s';
document.getElementById('peers-count').textContent=d.peerCount;
});
fetch('/api/peers').then(r=>r.json()).then(peers=>{
const sel=document.getElementById('target');
const old=sel.value;
sel.innerHTML='<option value="broadcast">All Devices</option>';
peers.forEach(p=>{sel.innerHTML+='<option value="'+p.name+'">'+p.name+'</option>';});
sel.value=old||'broadcast';
const list=document.getElementById('peer-list');
if(!peers.length){list.innerHTML='No peers discovered';return;}
list.innerHTML=peers.map(p=>'<div class="peer"><span><span class="dot '+(p.active?'on':'off')+'"></span>'+p.name+' ('+p.mac+')</span><span>'+(p.role==0?'HUB':'NODE')+' | '+p.lastSeen+'s ago</span></div>').join('');
});}
function send(){fetch('/api/send?target='+document.getElementById('target').value+'&cmd='+document.getElementById('cmd').value);}
function discover(){fetch('/api/discover');setTimeout(refresh,2000);}
refresh();setInterval(refresh,3000);
</script></body></html>
)rawliteral";
}

// ==================== BLE ====================

class BLECallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer* s) { bleConnected = true; }
  void onDisconnect(BLEServer* s) {
    bleConnected = false;
    s->getAdvertising()->start();
  }
};

class RoleCallback : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *c) {
    String val = String(c->getValue().c_str());
    deviceRole = val.toInt();
    prefs.begin("mesh", false);
    prefs.putInt("role", deviceRole);
    prefs.end();
    Serial.printf("Role set to %d\n", deviceRole);
  }
};

class NameCallback : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *c) {
    deviceName = String(c->getValue().c_str());
    prefs.begin("mesh", false);
    prefs.putString("name", deviceName);
    prefs.end();
    Serial.printf("Name set to %s\n", deviceName.c_str());
  }
};

class CmdCallback : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *c) {
    String cmd = String(c->getValue().c_str());
    Serial.printf("BLE CMD: %s\n", cmd.c_str());

    if (cmd == "DISCOVER") {
      sendDiscovery();
    } else if (cmd == "RESTART") {
      ESP.restart();
    } else if (cmd.startsWith("SEND:")) {
      // SEND:target:command
      int sep1 = cmd.indexOf(':', 5);
      if (sep1 > 0) {
        String target = cmd.substring(5, sep1);
        String command = cmd.substring(sep1 + 1);
        if (target == "ALL") {
          broadcast(MSG_COMMAND, command.c_str(), command.length());
        } else {
          for (int i = 0; i < peerCount; i++) {
            if (String(peers[i].name) == target) {
              sendCommand(peers[i].mac, command.c_str());
              break;
            }
          }
        }
      }
    }
    updateBLEStatus();
  }
};

class ESPNOWCmdCallback : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *c) {
    // Raw ESP-NOW send from Pi via BLE
    String val = String(c->getValue().c_str());
    broadcast(MSG_COMMAND, val.c_str(), val.length());
  }
};

void initBLE() {
  BLEDevice::init(deviceName.c_str());
  BLEServer *server = BLEDevice::createServer();
  server->setCallbacks(new BLECallbacks());

  BLEService *service = server->createService(SERVICE_UUID);

  // Role characteristic
  BLECharacteristic *roleChar = service->createCharacteristic(
    ROLE_CHAR_UUID, BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE);
  roleChar->setCallbacks(new RoleCallback());
  roleChar->setValue(String(deviceRole).c_str());

  // Name characteristic
  BLECharacteristic *nameChar = service->createCharacteristic(
    NAME_CHAR_UUID, BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE);
  nameChar->setCallbacks(new NameCallback());
  nameChar->setValue(deviceName.c_str());

  // Command characteristic
  BLECharacteristic *cmdChar = service->createCharacteristic(
    CMD_CHAR_UUID, BLECharacteristic::PROPERTY_WRITE);
  cmdChar->setCallbacks(new CmdCallback());

  // Status characteristic (read/notify)
  pStatusChar = service->createCharacteristic(
    STATUS_CHAR_UUID, BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
  pStatusChar->addDescriptor(new BLE2902());

  // Peers characteristic (read/notify)
  pPeersChar = service->createCharacteristic(
    PEERS_CHAR_UUID, BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
  pPeersChar->addDescriptor(new BLE2902());

  // ESP-NOW command passthrough
  BLECharacteristic *espnowChar = service->createCharacteristic(
    ESPNOW_CMD_CHAR_UUID, BLECharacteristic::PROPERTY_WRITE);
  espnowChar->setCallbacks(new ESPNOWCmdCallback());

  service->start();
  BLEAdvertising *adv = BLEDevice::getAdvertising();
  adv->addServiceUUID(SERVICE_UUID);
  adv->setScanResponse(true);
  BLEDevice::startAdvertising();

  Serial.println("BLE advertising as '" + deviceName + "'");
}

void updateBLEStatus() {
  if (pStatusChar) {
    String status = getStatusJSON();
    pStatusChar->setValue(status.c_str());
    if (bleConnected) pStatusChar->notify();
  }
  if (pPeersChar) {
    String pl = getPeerListJSON();
    pPeersChar->setValue(pl.c_str());
    if (bleConnected) pPeersChar->notify();
  }
}

// ==================== Serial Bridge (Pi Communication) ====================

void processSerial() {
  if (Serial.available()) {
    String line = Serial.readStringUntil('\n');
    line.trim();

    if (line.startsWith("CMD:")) {
      // CMD:target:command
      int sep1 = line.indexOf(':', 4);
      if (sep1 > 0) {
        String target = line.substring(4, sep1);
        String cmd = line.substring(sep1 + 1);
        if (target == "ALL") {
          broadcast(MSG_COMMAND, cmd.c_str(), cmd.length());
        } else {
          for (int i = 0; i < peerCount; i++) {
            if (String(peers[i].name) == target) {
              sendCommand(peers[i].mac, cmd.c_str());
              break;
            }
          }
        }
      }
    } else if (line == "DISCOVER") {
      sendDiscovery();
    } else if (line == "PEERS") {
      Serial.println("PEERS|" + getPeerListJSON());
    } else if (line == "STATUS") {
      Serial.println("STATUS|" + getStatusJSON());
    }
  }
}

// ==================== Setup & Loop ====================

void setup() {
  Serial.begin(115200);
  Serial.println("\n=== ESP32 Mesh ===");

  pinMode(4, OUTPUT);  // Flash LED
  digitalWrite(4, LOW);

  // Load saved config
  prefs.begin("mesh", true);
  deviceRole = prefs.getInt("role", ROLE_NODE);
  deviceName = prefs.getString("name", "esp32-node-1");
  prefs.end();

  getMACAddress();
  Serial.printf("MAC: %s\n", macToString(myMAC).c_str());
  Serial.printf("Role: %s\n", deviceRole == ROLE_HUB ? "HUB" : "NODE");
  Serial.printf("Name: %s\n", deviceName.c_str());

  // Init protocols
  if (deviceRole == ROLE_HUB) {
    initWiFiAP();
    setupWebServer();
  } else {
    initWiFiSTA();
  }

  initESPNOW();
  initBLE();

  // Initial discovery
  delay(1000);
  sendDiscovery();

  Serial.println("=== Ready ===");
}

void loop() {
  // Process incoming ESP-NOW messages
  processMessage();

  // Process Pi serial commands
  processSerial();

  // Web server (hub only)
  if (deviceRole == ROLE_HUB) {
    webServer.handleClient();
  }

  // Periodic discovery (every 30s)
  static unsigned long lastDiscovery = 0;
  if (millis() - lastDiscovery > 30000) {
    lastDiscovery = millis();
    sendDiscovery();
  }

  // Periodic BLE status update (every 5s)
  static unsigned long lastBLEUpdate = 0;
  if (millis() - lastBLEUpdate > 5000) {
    lastBLEUpdate = millis();
    updateBLEStatus();
  }

  // Mark peers inactive after 60s
  for (int i = 0; i < peerCount; i++) {
    if (millis() - peers[i].lastSeen > 60000) {
      peers[i].active = false;
    }
  }
}
