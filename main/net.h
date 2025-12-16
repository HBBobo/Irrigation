#pragma once
#include <WiFi.h>

static volatile bool wifiUp = false;
static const char* g_ssid = nullptr;
static const char* g_pass = nullptr;
static unsigned long g_lastReconnectAttempt = 0;
static const unsigned long RECONNECT_INTERVAL_MS = 30000;  // Retry every 30 seconds

static bool net_begin(const char* ssid, const char* pass) {
  g_ssid = ssid;
  g_pass = pass;

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(ssid, pass);

  Serial.print("[NET] connecting");
  unsigned long t0 = millis();

  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 20000) {
    Serial.print(".");
    delay(250);
    yield();
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    wifiUp = true;
    Serial.print("[NET] IP: ");
    Serial.println(WiFi.localIP());
    return true;
  }
  wifiUp = false;
  g_lastReconnectAttempt = millis();
  Serial.println("[NET] connection failed");
  return false;
}

// Returns true if WiFi just reconnected (for triggering webui update check)
static bool net_tryReconnect() {
  if (wifiUp) return false;
  if (g_ssid == nullptr) return false;

  unsigned long now = millis();
  if (now - g_lastReconnectAttempt < RECONNECT_INTERVAL_MS) return false;

  g_lastReconnectAttempt = now;
  Serial.print("[NET] reconnecting");

  WiFi.disconnect();
  WiFi.begin(g_ssid, g_pass);

  unsigned long t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 10000) {
    Serial.print(".");
    delay(250);
    yield();
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    wifiUp = true;
    Serial.print("[NET] IP: ");
    Serial.println(WiFi.localIP());
    return true;  // Signal that we just reconnected
  }

  Serial.println("[NET] reconnect failed");
  return false;
}

static bool net_isUp() {
  // Also check actual WiFi status in case connection dropped
  if (wifiUp && WiFi.status() != WL_CONNECTED) {
    wifiUp = false;
    Serial.println("[NET] connection lost");
  }
  return wifiUp;
}

static IPAddress net_ip() {
  return WiFi.localIP();
}
