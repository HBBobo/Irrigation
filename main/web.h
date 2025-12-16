#pragma once
#include <WebServer.h>
#include "config.h"
#include "net.h"
#include "storage.h"
#include "fs_api.h"

static WebServer webServer(80);
static Config*    gCfg  = nullptr;
static Runtime*   gRt   = nullptr;
static Histories* gHist = nullptr;

static String jbool(bool v) { return v ? "true" : "false"; }

// serve / from SD (/web/index.html)
static void handleRoot() {
  if (!storage_isReady() || !sd.exists("/web/index.html")) {
    webServer.send(200, "text/plain",
      "Web UI missing. Upload /web/index.html to SD or let GH download run.");
    return;
  }

  FsFile f = sd.open("/web/index.html", O_RDONLY);
  if (!f) { webServer.send(500, "text/plain", "Failed to open index.html"); return; }

  webServer.setContentLength(f.fileSize());
  webServer.sendHeader("Content-Type", "text/html; charset=utf-8");
  webServer.send(200);

  WiFiClient c = webServer.client();
  uint8_t buf[1024];
  while (f.available() && c.connected()) {
    int n = f.read(buf, sizeof(buf));
    if (n <= 0) break;
    c.write(buf, n);
    delay(0);
  }
  f.close();
}

static void handleStatus() {
  String json = "{";
  json += "\"fw\":\"" + String(FW_VERSION) + "\",";
  json += "\"wifi\":" + jbool(net_isUp()) + ",";
  json += "\"ip\":\"" + net_ip().toString() + "\",";
  json += "\"ip_static\":" + jbool(net_isStatic()) + ",";

  json += "\"soil\":" + String(gRt->soilNow) + ",";
  json += "\"tempC\":" + String((float)gRt->tempC_x10 / 10.0f, 1) + ",";
  json += "\"cpuPct\":" + String(gRt->cpuPct) + ",";

  json += "\"pumpOn\":" + jbool(gRt->pumpOn) + ",";
  json += "\"lockout\":" + jbool(gRt->lockout) + ",";
  json += "\"mode\":" + String((int)gCfg->mode);
  json += "}";
  webServer.send(200, "application/json", json);
}

static void handleGetConfig() {
  String json = "{";
  json += "\"dryOn\":" + String(gCfg->dryOn) + ",";
  json += "\"wetOff\":" + String(gCfg->wetOff) + ",";
  json += "\"pumpPwm\":" + String(gCfg->pumpPwm) + ",";
  json += "\"softRamp\":" + jbool(gCfg->softRamp) + ",";
  json += "\"minOnMs\":" + String((unsigned long)gCfg->minOnMs) + ",";
  json += "\"minOffMs\":" + String((unsigned long)gCfg->minOffMs) + ",";
  json += "\"limitWindowSec\":" + String((unsigned long)gCfg->limitWindowSec) + ",";
  json += "\"maxOnSecInWindow\":" + String((unsigned long)gCfg->maxOnSecInWindow) + ",";
  json += "\"logPeriodMs\":" + String((unsigned long)gCfg->logPeriodMs) + ",";
  json += "\"mode\":" + String((int)gCfg->mode);
  json += "}";
  webServer.send(200, "application/json", json);
}

static bool hasArg(const char* k) { return webServer.hasArg(k) && webServer.arg(k).length(); }

static void handleSetConfig() {
  Config old = *gCfg;

  if (hasArg("dryOn")) gCfg->dryOn = webServer.arg("dryOn").toInt();
  if (hasArg("wetOff")) gCfg->wetOff = webServer.arg("wetOff").toInt();
  if (hasArg("pumpPwm")) gCfg->pumpPwm = webServer.arg("pumpPwm").toInt();
  if (hasArg("softRamp")) gCfg->softRamp = webServer.arg("softRamp").toInt() != 0;

  if (hasArg("minOnMs")) gCfg->minOnMs = (uint32_t)webServer.arg("minOnMs").toInt();
  if (hasArg("minOffMs")) gCfg->minOffMs = (uint32_t)webServer.arg("minOffMs").toInt();

  if (hasArg("limitWindowSec")) gCfg->limitWindowSec = (uint32_t)webServer.arg("limitWindowSec").toInt();
  if (hasArg("maxOnSecInWindow")) gCfg->maxOnSecInWindow = (uint32_t)webServer.arg("maxOnSecInWindow").toInt();

  if (hasArg("logPeriodMs")) gCfg->logPeriodMs = (uint32_t)webServer.arg("logPeriodMs").toInt();

  if (hasArg("mode")) gCfg->mode = (PumpMode)webServer.arg("mode").toInt();

  // sanity
  if (gCfg->pumpPwm < 0) gCfg->pumpPwm = 0;
  if (gCfg->pumpPwm > 255) gCfg->pumpPwm = 255;
  if (gCfg->wetOff >= gCfg->dryOn) gCfg->wetOff = gCfg->dryOn - 50;
  if (gCfg->limitWindowSec < 5) gCfg->limitWindowSec = 5;
  if (gCfg->logPeriodMs < 1000) gCfg->logPeriodMs = 1000;
  if (gCfg->mode > PUMP_ON) gCfg->mode = PUMP_AUTO;

  storage_saveConfig(*gCfg);
  webServer.send(200, "text/plain", "OK");
}

static void handleHistory() {
  // returns ring buffer in chronological order
  String json = "{";
  json += "\"len\":" + String((int)HIST_LEN) + ",";
  json += "\"filled\":" + jbool(gHist->filled) + ",";
  json += "\"idx\":" + String(gHist->idx) + ",";

  auto emitArrayI16 = [&](const char* key, const int16_t* arr) {
    json += "\"" + String(key) + "\":[";
    uint16_t n = gHist->filled ? HIST_LEN : gHist->idx;
    for (uint16_t i = 0; i < n; i++) {
      uint16_t pos = gHist->filled ? (gHist->idx + i) % HIST_LEN : i;
      if (i) json += ",";
      json += String(arr[pos]);
    }
    json += "],";
  };

  auto emitArrayU8 = [&](const char* key, const uint8_t* arr) {
    json += "\"" + String(key) + "\":[";
    uint16_t n = gHist->filled ? HIST_LEN : gHist->idx;
    for (uint16_t i = 0; i < n; i++) {
      uint16_t pos = gHist->filled ? (gHist->idx + i) % HIST_LEN : i;
      if (i) json += ",";
      json += String(arr[pos]);
    }
    json += "],";
  };

  emitArrayI16("soil", gHist->soil);
  emitArrayI16("tempC_x10", gHist->tempC_x10);
  emitArrayU8("cpuPct", gHist->cpuPct);

  // remove trailing comma
  if (json.endsWith(",")) json.remove(json.length() - 1);
  json += "}";
  webServer.send(200, "application/json", json);
}

static void handleRestart() {
  webServer.send(200, "text/plain", "Restarting...");
  delay(200);
  ESP.restart();
}

static void web_begin(Config* cfg, Runtime* rt, Histories* hist) {
  gCfg = cfg; gRt = rt; gHist = hist;

  webServer.on("/", HTTP_GET, handleRoot);
  webServer.on("/api/status", HTTP_GET, handleStatus);
  webServer.on("/api/config/get", HTTP_GET, handleGetConfig);
  webServer.on("/api/config/set", HTTP_POST, handleSetConfig);
  webServer.on("/api/history", HTTP_GET, handleHistory);
  webServer.on("/api/restart", HTTP_POST, handleRestart);

  fs_register(webServer);

  webServer.begin();
  Serial.println("[WEB] server started");
}

static void web_loop() {
  webServer.handleClient();
}
