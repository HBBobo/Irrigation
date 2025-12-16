#pragma once
#include <WiFi.h>

static void net_begin(const char* ssid, const char* pass) {
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);

  Serial.print("[NET] Starting WiFi ");
  Serial.println(ssid);

  WiFi.begin(ssid, pass);
}

static bool net_isUp() {
  return WiFi.status() == WL_CONNECTED;
}

static IPAddress net_ip() {
  return WiFi.localIP();
}

// compatibility field for your UI JSON
static bool net_isStatic() { return false; }
