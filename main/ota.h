#pragma once
#include <ArduinoOTA.h>

static void ota_begin() {
  ArduinoOTA.setHostname("esp32-irrigation");
  ArduinoOTA.setPasswordHash("8602d8eeb9527dbdbd36743672a730a2");
  ArduinoOTA.begin();
  Serial.println("[OTA] ready");
}

static void ota_loop() {
  ArduinoOTA.handle();
}
