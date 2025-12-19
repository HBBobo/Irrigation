#pragma once
#include <ArduinoOTA.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <Update.h>
#include <esp_task_wdt.h>
#include "credentials.h"

// Current firmware version - update this when releasing new versions
#define FIRMWARE_VERSION "1.0.5"

// Path to store running firmware version on SD
static const char* OTA_VERSION_FILE = "/firmware.version";

// firmware.json URL
static const char* OTA_FIRMWARE_JSON_URL =
  "https://raw.githubusercontent.com/HBBobo/Irrigation/main/firmware/firmware.json";

// Check interval: 6 hours in milliseconds
static const uint32_t OTA_CHECK_INTERVAL_MS = 6UL * 60UL * 60UL * 1000UL;

// Track last check time
static uint32_t g_lastOtaCheckMs = 0;
static bool g_otaCheckedOnBoot = false;

// Compare version strings (e.g., "1.0.1" vs "1.0.2")
static int ota_compareVersions(const String& v1, const String& v2) {
  int major1 = 0, minor1 = 0, patch1 = 0;
  int major2 = 0, minor2 = 0, patch2 = 0;

  sscanf(v1.c_str(), "%d.%d.%d", &major1, &minor1, &patch1);
  sscanf(v2.c_str(), "%d.%d.%d", &major2, &minor2, &patch2);

  if (major1 != major2) return major1 < major2 ? -1 : 1;
  if (minor1 != minor2) return minor1 < minor2 ? -1 : 1;
  if (patch1 != patch2) return patch1 < patch2 ? -1 : 1;
  return 0;
}

// Fetch firmware info from GitHub
// Returns remote version string and populates url with download URL
static String ota_getRemoteFirmwareInfo(String& url) {
  WiFiClientSecure client;
  client.setInsecure();  // Skip SSL verification
  client.setTimeout(10);

  HTTPClient http;
  http.setTimeout(15000);

  Serial.print("[OTA] Checking for firmware update...");

  if (!http.begin(client, OTA_FIRMWARE_JSON_URL)) {
    Serial.println(" begin failed");
    return "";
  }

  int code = http.GET();
  if (code != 200) {
    Serial.printf(" HTTP %d\n", code);
    http.end();
    return "";
  }

  String json = http.getString();
  http.end();
  Serial.println(" OK");

  // Parse "firmware" section
  int fwIdx = json.indexOf("\"firmware\"");
  if (fwIdx < 0) return "";

  // Find "version" within firmware section
  int versionIdx = json.indexOf("\"version\"", fwIdx);
  if (versionIdx < 0) return "";

  int quoteStart = json.indexOf("\"", versionIdx + 9);
  if (quoteStart < 0) return "";

  int quoteEnd = json.indexOf("\"", quoteStart + 1);
  if (quoteEnd < 0) return "";

  String version = json.substring(quoteStart + 1, quoteEnd);

  // Find "url" within firmware section
  int urlIdx = json.indexOf("\"url\"", fwIdx);
  if (urlIdx < 0) return version;

  int urlQuoteStart = json.indexOf("\"", urlIdx + 5);
  if (urlQuoteStart < 0) return version;

  int urlQuoteEnd = json.indexOf("\"", urlQuoteStart + 1);
  if (urlQuoteEnd < 0) return version;

  url = json.substring(urlQuoteStart + 1, urlQuoteEnd);

  return version;
}

// Download and flash firmware from URL
static bool ota_performUpdate(const String& url) {
  Serial.printf("[OTA] Downloading firmware from: %s\n", url.c_str());

  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(30);

  HTTPClient http;
  http.setTimeout(60000);  // 60 second timeout for large file

  if (!http.begin(client, url)) {
    Serial.println("[OTA] HTTP begin failed");
    return false;
  }

  int code = http.GET();
  if (code != 200) {
    Serial.printf("[OTA] HTTP error: %d\n", code);
    http.end();
    return false;
  }

  int contentLength = http.getSize();
  if (contentLength <= 0) {
    Serial.println("[OTA] Invalid content length");
    http.end();
    return false;
  }

  Serial.printf("[OTA] Firmware size: %d bytes\n", contentLength);

  // Check if there's enough space
  if (!Update.begin(contentLength)) {
    Serial.printf("[OTA] Not enough space: %s\n", Update.errorString());
    http.end();
    return false;
  }

  Serial.println("[OTA] Flashing firmware...");

  WiFiClient* stream = http.getStreamPtr();
  uint8_t buf[1024];
  size_t written = 0;
  int lastPercent = -1;

  while (http.connected() && written < (size_t)contentLength) {
    // Feed watchdog during long operation
    esp_task_wdt_reset();

    size_t available = stream->available();
    if (available) {
      size_t toRead = min(available, sizeof(buf));
      size_t bytesRead = stream->readBytes(buf, toRead);

      if (bytesRead > 0) {
        size_t bytesWritten = Update.write(buf, bytesRead);
        if (bytesWritten != bytesRead) {
          Serial.printf("[OTA] Write error: %s\n", Update.errorString());
          Update.abort();
          http.end();
          return false;
        }
        written += bytesWritten;

        // Progress indicator
        int percent = (written * 100) / contentLength;
        if (percent != lastPercent && percent % 10 == 0) {
          Serial.printf("[OTA] Progress: %d%%\n", percent);
          lastPercent = percent;
        }
      }
    }
    delay(1);  // Yield
  }

  http.end();

  if (written != (size_t)contentLength) {
    Serial.printf("[OTA] Size mismatch: got %u, expected %d\n", (unsigned)written, contentLength);
    Update.abort();
    return false;
  }

  if (!Update.end(true)) {
    Serial.printf("[OTA] Update end failed: %s\n", Update.errorString());
    return false;
  }

  Serial.println("[OTA] Update successful! Rebooting...");
  return true;
}

// Check for and apply firmware update
static void ota_checkForUpdate() {
  String url;
  String remoteVersion = ota_getRemoteFirmwareInfo(url);

  if (remoteVersion.length() == 0) {
    Serial.println("[OTA] Could not get remote version");
    return;
  }

  Serial.printf("[OTA] Current: %s, Remote: %s\n", FIRMWARE_VERSION, remoteVersion.c_str());

  if (ota_compareVersions(FIRMWARE_VERSION, remoteVersion) >= 0) {
    Serial.println("[OTA] Firmware is up to date");
    return;
  }

  Serial.println("[OTA] New firmware available!");

  if (url.length() == 0) {
    Serial.println("[OTA] No download URL found");
    return;
  }

  if (ota_performUpdate(url)) {
    delay(1000);
    ESP.restart();
  }
}

// Save current firmware version to SD card
static void ota_saveVersion() {
  FsFile f = sd.open(OTA_VERSION_FILE, O_WRITE | O_CREAT | O_TRUNC);
  if (f) {
    f.print(FIRMWARE_VERSION);
    f.close();
    Serial.printf("[OTA] Saved version %s to SD\n", FIRMWARE_VERSION);
  }
}

static void ota_begin() {
  // Save current firmware version to SD
  ota_saveVersion();

  // Local network OTA (Arduino IDE)
  ArduinoOTA.setPassword(OTA_PASSWORD);

  ArduinoOTA.onStart([]() {
    Serial.println("[OTA] Local update starting...");
  });

  ArduinoOTA.onEnd([]() {
    Serial.println("\n[OTA] Local update complete!");
  });

  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("[OTA] Progress: %u%%\r", (progress / (total / 100)));
  });

  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("[OTA] Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed");
  });

  ArduinoOTA.begin();
  Serial.printf("[OTA] Ready (firmware v%s)\n", FIRMWARE_VERSION);
}

static void ota_loop() {
  // Handle local network OTA
  ArduinoOTA.handle();

  uint32_t now = millis();

  // Check on boot (after a short delay to let things stabilize)
  if (!g_otaCheckedOnBoot && now > 10000) {
    g_otaCheckedOnBoot = true;
    g_lastOtaCheckMs = now;
    ota_checkForUpdate();
    return;
  }

  // Check every 6 hours
  if (now - g_lastOtaCheckMs >= OTA_CHECK_INTERVAL_MS) {
    g_lastOtaCheckMs = now;
    ota_checkForUpdate();
  }
}
