#pragma once
#include <WiFi.h>

struct NetState {
  bool usingStatic = false;
  IPAddress ip;
};

static NetState net;

// próbál statikus IP-vel, ha nem megy → DHCP
static void net_begin(
  const char* ssid,
  const char* pass,
  IPAddress staticIP,
  IPAddress gateway,
  IPAddress subnet,
  unsigned long timeoutMs = 5000
) {
  Serial.println("[NET] trying static IP...");
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);

  WiFi.config(staticIP, gateway, subnet);
  WiFi.begin(ssid, pass);

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < timeoutMs) {
    delay(200);
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    net.usingStatic = true;
    net.ip = WiFi.localIP();
    Serial.println("\n[NET] static IP OK");
    Serial.println(net.ip);
    return;
  }

  // fallback DHCP
  Serial.println("\n[NET] static failed → DHCP fallback");
  WiFi.disconnect(true);
  delay(200);

  WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE);
  WiFi.begin(ssid, pass);

  while (WiFi.status() != WL_CONNECTED) {
    delay(200);
    Serial.print(".");
  }

  net.usingStatic = false;
  net.ip = WiFi.localIP();
  Serial.println("\n[NET] DHCP IP OK");
  Serial.println(net.ip);
}

static IPAddress net_ip() {
  return net.ip;
}

static bool net_isStatic() {
  return net.usingStatic;
}
