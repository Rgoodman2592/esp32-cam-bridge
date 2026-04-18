/*
 * ESP32-CAM Bridge Firmware
 * Dual-mode: WiFi (streaming + web UI) + BLE (configuration)
 * Built for Inland ESP32-CAM
 */

#include "esp_camera.h"
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include "camera_pins.h"
#include "web_ui.h"

// ----- Defaults (overridden by BLE or saved prefs) -----
String wifi_ssid = "";
String wifi_pass = "";
String device_name = "esp32-cam-bridge";

// ----- State -----
bool wifiConnected = false;
bool bleActive = true;
int streamClients = 0;

// Camera settings (adjustable via web UI)
int frameSize = FRAMESIZE_VGA;      // 640x480 default
int jpegQuality = 12;                // 0-63 (lower = better quality)
int brightness = 0;                  // -2 to 2
int contrast = 0;                    // -2 to 2
int saturation = 0;                  // -2 to 2
bool hmirror = false;
bool vflip = false;
bool flashOn = false;

Preferences prefs;
WebServer server(80);
WebServer streamServer(81);

// ----- BLE UUIDs -----
#define SERVICE_UUID        "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define SSID_CHAR_UUID      "beb5483e-36e1-4688-b7f5-ea07361b26a8"
#define PASS_CHAR_UUID      "beb5483e-36e1-4688-b7f5-ea07361b26a9"
#define CMD_CHAR_UUID       "beb5483e-36e1-4688-b7f5-ea07361b26aa"
#define STATUS_CHAR_UUID    "beb5483e-36e1-4688-b7f5-ea07361b26ab"

BLECharacteristic *pStatusChar;
bool deviceConnectedBLE = false;

// ==================== BLE Callbacks ====================

class ServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer* pServer) { deviceConnectedBLE = true; }
  void onDisconnect(BLEServer* pServer) {
    deviceConnectedBLE = false;
    pServer->getAdvertising()->start();
  }
};

class SSIDCallback : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *pChar) {
    wifi_ssid = String(pChar->getValue().c_str());
    Serial.printf("BLE: SSID set to '%s'\n", wifi_ssid.c_str());
  }
};

class PassCallback : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *pChar) {
    wifi_pass = String(pChar->getValue().c_str());
    Serial.println("BLE: Password updated");
  }
};

class CmdCallback : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *pChar) {
    String cmd = String(pChar->getValue().c_str());
    Serial.printf("BLE: Command '%s'\n", cmd.c_str());

    if (cmd == "SAVE") {
      prefs.begin("wifi", false);
      prefs.putString("ssid", wifi_ssid);
      prefs.putString("pass", wifi_pass);
      prefs.end();
      updateBLEStatus("SAVED");
      Serial.println("WiFi credentials saved");
    } else if (cmd == "CONNECT") {
      connectWiFi();
    } else if (cmd == "RESTART") {
      ESP.restart();
    } else if (cmd == "STATUS") {
      String status = wifiConnected ? "CONNECTED:" + WiFi.localIP().toString() : "DISCONNECTED";
      updateBLEStatus(status);
    }
  }
};

void updateBLEStatus(String status) {
  if (pStatusChar) {
    pStatusChar->setValue(status.c_str());
    if (deviceConnectedBLE) {
      pStatusChar->notify();
    }
  }
}

// ==================== WiFi ====================

void connectWiFi() {
  if (wifi_ssid.length() == 0) {
    Serial.println("No SSID configured");
    updateBLEStatus("NO_SSID");
    return;
  }

  Serial.printf("Connecting to '%s'...\n", wifi_ssid.c_str());
  updateBLEStatus("CONNECTING");

  WiFi.mode(WIFI_STA);
  WiFi.begin(wifi_ssid.c_str(), wifi_pass.c_str());

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 30) {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    wifiConnected = true;
    Serial.printf("\nConnected! IP: %s\n", WiFi.localIP().toString().c_str());
    updateBLEStatus("CONNECTED:" + WiFi.localIP().toString());
  } else {
    wifiConnected = false;
    Serial.println("\nFailed to connect");
    updateBLEStatus("FAILED");
  }
}

// ==================== Camera ====================

void initCamera() {
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;
  config.grab_mode = CAMERA_GRAB_LATEST;

  if (psramFound()) {
    config.frame_size = (framesize_t)frameSize;
    config.jpeg_quality = jpegQuality;
    config.fb_count = 2;
    config.fb_location = CAMERA_FB_IN_PSRAM;
    Serial.println("PSRAM found, using dual framebuffer");
  } else {
    config.frame_size = FRAMESIZE_VGA;
    config.jpeg_quality = 15;
    config.fb_count = 1;
    config.fb_location = CAMERA_FB_IN_DRAM;
    Serial.println("No PSRAM, limited to VGA");
  }

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed: 0x%x\n", err);
    return;
  }

  sensor_t *s = esp_camera_sensor_get();
  if (s) {
    s->set_brightness(s, brightness);
    s->set_contrast(s, contrast);
    s->set_saturation(s, saturation);
    s->set_hmirror(s, hmirror ? 1 : 0);
    s->set_vflip(s, vflip ? 1 : 0);
  }

  Serial.println("Camera initialized");
}

// ==================== Web Server Handlers ====================

void handleRoot() {
  server.send(200, "text/html", WEB_UI_HTML);
}

void handleStatus() {
  String json = "{";
  json += "\"wifi\":" + String(wifiConnected ? "true" : "false") + ",";
  json += "\"ip\":\"" + (wifiConnected ? WiFi.localIP().toString() : String("N/A")) + "\",";
  json += "\"rssi\":" + String(WiFi.RSSI()) + ",";
  json += "\"ble\":" + String(bleActive ? "true" : "false") + ",";
  json += "\"bleConnected\":" + String(deviceConnectedBLE ? "true" : "false") + ",";
  json += "\"streamClients\":" + String(streamClients) + ",";
  json += "\"frameSize\":" + String(frameSize) + ",";
  json += "\"quality\":" + String(jpegQuality) + ",";
  json += "\"brightness\":" + String(brightness) + ",";
  json += "\"contrast\":" + String(contrast) + ",";
  json += "\"saturation\":" + String(saturation) + ",";
  json += "\"hmirror\":" + String(hmirror ? "true" : "false") + ",";
  json += "\"vflip\":" + String(vflip ? "true" : "false") + ",";
  json += "\"flash\":" + String(flashOn ? "true" : "false") + ",";
  json += "\"uptime\":" + String(millis() / 1000) + ",";
  json += "\"freeHeap\":" + String(ESP.getFreeHeap()) + ",";
  json += "\"psram\":" + String(psramFound() ? ESP.getFreePsram() : 0);
  json += "}";
  server.send(200, "application/json", json);
}

void handleSettings() {
  if (server.hasArg("framesize")) {
    frameSize = server.arg("framesize").toInt();
    sensor_t *s = esp_camera_sensor_get();
    if (s) s->set_framesize(s, (framesize_t)frameSize);
  }
  if (server.hasArg("quality")) {
    jpegQuality = server.arg("quality").toInt();
    sensor_t *s = esp_camera_sensor_get();
    if (s) s->set_quality(s, jpegQuality);
  }
  if (server.hasArg("brightness")) {
    brightness = server.arg("brightness").toInt();
    sensor_t *s = esp_camera_sensor_get();
    if (s) s->set_brightness(s, brightness);
  }
  if (server.hasArg("contrast")) {
    contrast = server.arg("contrast").toInt();
    sensor_t *s = esp_camera_sensor_get();
    if (s) s->set_contrast(s, contrast);
  }
  if (server.hasArg("saturation")) {
    saturation = server.arg("saturation").toInt();
    sensor_t *s = esp_camera_sensor_get();
    if (s) s->set_saturation(s, saturation);
  }
  if (server.hasArg("hmirror")) {
    hmirror = server.arg("hmirror") == "1";
    sensor_t *s = esp_camera_sensor_get();
    if (s) s->set_hmirror(s, hmirror ? 1 : 0);
  }
  if (server.hasArg("vflip")) {
    vflip = server.arg("vflip") == "1";
    sensor_t *s = esp_camera_sensor_get();
    if (s) s->set_vflip(s, vflip ? 1 : 0);
  }
  if (server.hasArg("flash")) {
    flashOn = server.arg("flash") == "1";
    digitalWrite(FLASH_GPIO_NUM, flashOn ? HIGH : LOW);
  }

  server.send(200, "application/json", "{\"ok\":true}");
}

void handleCapture() {
  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) {
    server.send(500, "text/plain", "Camera capture failed");
    return;
  }
  server.sendHeader("Content-Disposition", "inline; filename=capture.jpg");
  server.send_P(200, "image/jpeg", (const char *)fb->buf, fb->len);
  esp_camera_fb_return(fb);
}

void handleStream() {
  WiFiClient client = streamServer.client();
  streamClients++;

  String response = "HTTP/1.1 200 OK\r\n";
  response += "Content-Type: multipart/x-mixed-replace; boundary=frame\r\n";
  response += "Access-Control-Allow-Origin: *\r\n\r\n";
  client.print(response);

  while (client.connected()) {
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) {
      Serial.println("Frame capture failed");
      break;
    }

    String header = "--frame\r\nContent-Type: image/jpeg\r\nContent-Length: " + String(fb->len) + "\r\n\r\n";
    client.print(header);
    client.write(fb->buf, fb->len);
    client.print("\r\n");
    esp_camera_fb_return(fb);

    if (!client.connected()) break;
    delay(10);
  }

  streamClients--;
}

// ==================== BLE Setup ====================

void initBLE() {
  BLEDevice::init(device_name.c_str());
  BLEServer *pServer = BLEDevice::createServer();
  pServer->setCallbacks(new ServerCallbacks());

  BLEService *pService = pServer->createService(SERVICE_UUID);

  BLECharacteristic *pSSID = pService->createCharacteristic(
    SSID_CHAR_UUID,
    BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE
  );
  pSSID->setCallbacks(new SSIDCallback());
  pSSID->setValue(wifi_ssid.c_str());

  BLECharacteristic *pPass = pService->createCharacteristic(
    PASS_CHAR_UUID,
    BLECharacteristic::PROPERTY_WRITE
  );
  pPass->setCallbacks(new PassCallback());

  BLECharacteristic *pCmd = pService->createCharacteristic(
    CMD_CHAR_UUID,
    BLECharacteristic::PROPERTY_WRITE
  );
  pCmd->setCallbacks(new CmdCallback());

  pStatusChar = pService->createCharacteristic(
    STATUS_CHAR_UUID,
    BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY
  );
  pStatusChar->addDescriptor(new BLE2902());
  pStatusChar->setValue("READY");

  pService->start();

  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  pAdvertising->setMinPreferred(0x06);
  pAdvertising->setMinPreferred(0x12);
  BLEDevice::startAdvertising();

  Serial.println("BLE advertising as '" + device_name + "'");
}

// ==================== Setup & Loop ====================

void setup() {
  Serial.begin(115200);
  Serial.println("\n=== ESP32-CAM Bridge ===");

  pinMode(FLASH_GPIO_NUM, OUTPUT);
  digitalWrite(FLASH_GPIO_NUM, LOW);

  prefs.begin("wifi", true);
  wifi_ssid = prefs.getString("ssid", "");
  wifi_pass = prefs.getString("pass", "");
  prefs.end();

  initCamera();
  initBLE();

  if (wifi_ssid.length() > 0) {
    connectWiFi();
  } else {
    Serial.println("No WiFi configured - use BLE to set credentials");
  }

  server.on("/", handleRoot);
  server.on("/status", handleStatus);
  server.on("/settings", HTTP_GET, handleSettings);
  server.on("/capture", handleCapture);
  server.begin();

  streamServer.on("/stream", handleStream);
  streamServer.begin();

  Serial.println("Web UI: http://" + WiFi.localIP().toString());
  Serial.println("Stream: http://" + WiFi.localIP().toString() + ":81/stream");
  Serial.println("=== Ready ===");
}

void loop() {
  server.handleClient();
  streamServer.handleClient();

  static unsigned long lastCheck = 0;
  if (wifiConnected && WiFi.status() != WL_CONNECTED && millis() - lastCheck > 10000) {
    lastCheck = millis();
    Serial.println("WiFi lost, reconnecting...");
    connectWiFi();
  }
}
