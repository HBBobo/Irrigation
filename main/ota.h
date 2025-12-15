#pragma once
#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <Update.h>

// ===== USER CONFIG =====
static const char* OTA_FIRMWARE_JSON =
  "https://raw.githubusercontent.com/USER/REPO/main/firmware/firmware.json";

// milyen gyakran nézzen rá automatikusan (ms)
static const unsigned long OTA_CHECK_INTERVAL_MS = 6UL * 60UL * 60UL * 1000UL; // 6 óra

// ===== STATE =====
static unsigned long lastOtaCheckMs = 0;
static bool otaInProgress = false;

// verzió stringet a main.ino-ban definiáljuk
extern const char* FW_VERSION;

// ===== INTERNAL =====
struct OtaInfo {
  String version;
  String url;
};

// egyszerű JSON parser (direkt minimal, nincs ArduinoJson)
static bool parseFirmwareJson(const String& body, OtaInfo& out) {
  int vPos = body.indexOf("\"version\"");
  int uPos = body.indexOf("\"url\"");
  if (vPos < 0 || uPos < 0) return false;

  int vStart = body.indexOf('"', vPos + 9) + 1;
  int vEnd   = body.indexOf('"', vStart);
  int uStart = body.indexOf('"', uPos + 5) + 1;
  int uEnd   = body.indexOf('"', uStart);

  if (vStart <= 0 || uStart <= 0) return false;

  out.version = body.substring(vStart, vEnd);
  out.url     = body.substring(uStart, uEnd);
  return true;
}

static bool ota_isNewer(const String& remote) {
  return remote != String(FW_VERSION);
}

// ===== CORE OTA =====
static bool ota_perform(const String& binUrl) {
  otaInProgress = true;

  WiFiClientSecure client;
  client.setInsecure(); // GitHub HTTPS (root CA nélkül)

  HTTPClient http;
  Serial.println("[OTA] downloading firmware...");
  if (!http.begin(client, binUrl)) {
    Serial.println("[OTA] http begin failed");
    otaInProgress = false;
    return false;
  }

  int httpCode = http.GET();
  if (httpCode != HTTP_CODE_OK) {
    Serial.printf("[OTA] HTTP error: %d\n", httpCode);
    http.end();
    otaInProgress = false;
    return false;
  }

  int len = http.getSize();
  if (!Update.begin(len)) {
    Serial.println("[OTA] Update.begin failed");
    http.end();
    otaInProgress = false;
    return false;
  }

  WiFiClient* stream = http.getStreamPtr();
  size_t written = Update.writeStream(*stream);

  if (written != (size_t)len) {
    Serial.printf("[OTA] write mismatch %u / %d\n", (unsigned)written, len);
    Update.abort();
    http.end();
    otaInProgress = false;
    return false;
  }

  if (!Update.end(true)) {
    Serial.printf("[OTA] Update.end failed err=%u\n", Update.getError());
    http.end();
    otaInProgress = false;
    return false;
  }

  Serial.println("[OTA] update OK, rebooting...");
  http.end();
  delay(500);
  ESP.restart();
  return true; // sosem ér ide
}

// ===== CHECK =====
static bool ota_checkAndUpdate(bool force = false) {
  if (otaInProgress) return false;

  Serial.println("[OTA] checking firmware.json");

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  if (!http.begin(client, OTA_FIRMWARE_JSON)) {
    Serial.println("[OTA] http begin failed");
    return false;
  }

  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    Serial.printf("[OTA] firmware.json HTTP %d\n", code);
    http.end();
    return false;
  }

  String body = http.getString();
  http.end();

  OtaInfo info;
  if (!parseFirmwareJson(body, info)) {
    Serial.println("[OTA] json parse failed");
    return false;
  }

  Serial.printf("[OTA] local=%s remote=%s\n",
                FW_VERSION, info.version.c_str());

  if (force || ota_isNewer(info.version)) {
    Serial.println("[OTA] update needed");
    return ota_perform(info.url);
  }

  Serial.println("[OTA] already up-to-date");
  return true;
}

// ===== LOOP HOOK =====
static void ota_loop() {
  if (otaInProgress) return;

  unsigned long now = millis();
  if (now - lastOtaCheckMs >= OTA_CHECK_INTERVAL_MS) {
    lastOtaCheckMs = now;
    ota_checkAndUpdate(false);
  }
}

// ===== WEB TRIGGERS =====
static void ota_triggerManual() {
  ota_checkAndUpdate(true);
}

static bool ota_busy() {
  return otaInProgress;
}
