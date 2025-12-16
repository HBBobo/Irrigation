#pragma once
#include <WiFi.h>

static volatile bool wifiUp = false;

static bool net_begin(const char* ssid, const char* pass) {
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
  Serial.println("[NET] connection failed");
  return false;
}

static bool net_isUp() {
  // Also check actual WiFi status in case connection dropped
  if (wifiUp && WiFi.status() != WL_CONNECTED) {
    wifiUp = false;
  }
  return wifiUp;
}

static IPAddress net_ip() {
  return WiFi.localIP();
}
