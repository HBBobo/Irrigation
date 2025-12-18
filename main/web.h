#pragma once
#include <WebServer.h>
#include <esp_task_wdt.h>
#include "fs_api.h"
#include "config.h"

static WebServer webServer(80);

static Config*    gCfg;
static Runtime*   gRt;
static Histories* gHist;

// GET /api/status - real-time sensor data (matches webui expectations)
static void handleStatus() {
  char json[256];
  snprintf(json, sizeof(json),
    "{\"soil\":%d,\"tempC\":%.1f,\"cpuPct\":%u,\"pumpOn\":%s,\"lockout\":%s,\"mode\":%d,\"onTime\":%lu}",
    gRt->soilNow,
    gRt->tempC_x10 / 10.0f,
    gRt->cpuPct,
    gRt->pumpOn ? "true" : "false",
    gRt->lockout ? "true" : "false",
    (int)gCfg->mode,
    (unsigned long)(gRt->onTimeThisWindowMs / 1000)
  );
  webServer.send(200, "application/json", json);
}

// GET /api/config/get - get full config
static void handleGetConfig() {
  char json[512];
  snprintf(json, sizeof(json),
    "{\"dryOn\":%d,\"wetOff\":%d,\"pumpPwm\":%d,\"softRamp\":%s,"
    "\"minOnMs\":%lu,\"minOffMs\":%lu,\"limitWindowSec\":%lu,"
    "\"maxOnSecInWindow\":%lu,\"logPeriodMs\":%lu,\"mode\":%d}",
    gCfg->dryOn,
    gCfg->wetOff,
    gCfg->pumpPwm,
    gCfg->softRamp ? "true" : "false",
    (unsigned long)gCfg->minOnMs,
    (unsigned long)gCfg->minOffMs,
    (unsigned long)gCfg->limitWindowSec,
    (unsigned long)gCfg->maxOnSecInWindow,
    (unsigned long)gCfg->logPeriodMs,
    (int)gCfg->mode
  );
  webServer.send(200, "application/json", json);
}

// POST /api/config/set - update config
static void handleSetConfig() {
  bool changed = false;

  if (webServer.hasArg("dryOn")) {
    gCfg->dryOn = webServer.arg("dryOn").toInt();
    changed = true;
  }
  if (webServer.hasArg("wetOff")) {
    gCfg->wetOff = webServer.arg("wetOff").toInt();
    changed = true;
  }
  if (webServer.hasArg("pumpPwm")) {
    gCfg->pumpPwm = webServer.arg("pumpPwm").toInt();
    changed = true;
  }
  if (webServer.hasArg("mode")) {
    gCfg->mode = (PumpMode)webServer.arg("mode").toInt();
    changed = true;
  }
  if (webServer.hasArg("softRamp")) {
    gCfg->softRamp = webServer.arg("softRamp").toInt() != 0;
    changed = true;
  }
  if (webServer.hasArg("minOnMs")) {
    gCfg->minOnMs = webServer.arg("minOnMs").toInt();
    changed = true;
  }
  if (webServer.hasArg("minOffMs")) {
    gCfg->minOffMs = webServer.arg("minOffMs").toInt();
    changed = true;
  }
  if (webServer.hasArg("maxOnSecInWindow")) {
    gCfg->maxOnSecInWindow = webServer.arg("maxOnSecInWindow").toInt();
    changed = true;
  }
  if (webServer.hasArg("limitWindowSec")) {
    gCfg->limitWindowSec = webServer.arg("limitWindowSec").toInt();
    changed = true;
  }

  if (changed) {
    storage_validateConfig(*gCfg);
    storage_saveConfig(*gCfg);
    Serial.println("[WEB] Config updated");
  }

  webServer.send(200, "application/json", "{\"ok\":true}");
}

// POST /api/restart - restart ESP
static void handleRestart() {
  webServer.send(200, "application/json", "{\"ok\":true}");
  webServer.client().flush();
  delay(100);
  ESP.restart();
}

// GET /api/history - sensor history
static void handleHistory() {
  // Reset watchdog before long operation
  esp_task_wdt_reset();

  int count = gHist->filled ? HIST_LEN : gHist->idx;

  // Build JSON in a buffer - more reliable than streaming
  // Estimate: 240 items * 3 arrays * ~8 chars each = ~6KB max
  String json;
  json.reserve(8192);

  json = "{\"len\":";
  json += count;
  json += ",\"idx\":";
  json += gHist->idx;
  json += ",\"soil\":[";

  for (int i = 0; i < count; i++) {
    if (i > 0) json += ",";
    json += gHist->soil[i];
  }

  json += "],\"temp\":[";
  for (int i = 0; i < count; i++) {
    if (i > 0) json += ",";
    json += String(gHist->tempC_x10[i] / 10.0f, 1);
  }

  json += "],\"cpu\":[";
  for (int i = 0; i < count; i++) {
    if (i > 0) json += ",";
    json += gHist->cpuPct[i];
  }

  json += "]}";

  webServer.send(200, "application/json", json);
}

// POST /api/webui/update - force re-download webui from GitHub
static void handleWebuiUpdate() {
  // Delete version file to force re-download
  if (sd.exists("/web/.version")) sd.remove("/web/.version");

  webServer.send(200, "application/json", "{\"ok\":true,\"msg\":\"Restarting to update...\"}");
  webServer.client().flush();
  delay(100);
  ESP.restart();
}

// Serve static files from SD card
static void handleStaticFile(const char* path, const char* contentType) {
  FsFile f = sd.open(path, O_RDONLY);
  if (!f) {
    webServer.send(404, "text/plain", "File not found");
    return;
  }

  webServer.setContentLength(f.size());
  webServer.send(200, contentType, "");

  uint8_t buf[1024];
  while (f.available()) {
    size_t n = f.read(buf, sizeof(buf));
    if (n == 0) break;
    webServer.client().write(buf, n);
    delay(0);
  }

  f.close();
}

static void handleRoot() {
  handleStaticFile("/web/index.html", "text/html");
}

static void handleAppJs() {
  handleStaticFile("/web/app.js", "application/javascript");
}

static void handleStyleCss() {
  handleStaticFile("/web/style.css", "text/css");
}

static void web_begin(Config* cfg, Runtime* rt, Histories* hist) {
  gCfg  = cfg;
  gRt   = rt;
  gHist = hist;

  // Static files
  webServer.on("/", HTTP_GET, handleRoot);
  webServer.on("/app.js", HTTP_GET, handleAppJs);
  webServer.on("/style.css", HTTP_GET, handleStyleCss);

  // API endpoints
  webServer.on("/api/status", HTTP_GET, handleStatus);
  webServer.on("/api/config/get", HTTP_GET, handleGetConfig);
  webServer.on("/api/config/set", HTTP_POST, handleSetConfig);
  webServer.on("/api/history", HTTP_GET, handleHistory);
  webServer.on("/api/restart", HTTP_POST, handleRestart);
  webServer.on("/api/webui/update", HTTP_POST, handleWebuiUpdate);
  webServer.on("/api/webui/update", HTTP_GET, handleWebuiUpdate);  // Also allow GET for easy browser trigger

  // File browser
  fs_register(webServer);

  webServer.begin();
  Serial.println("[WEB] server started");
}

static void web_loop() {
  webServer.handleClient();
}

static void web_stop() {
  webServer.stop();
  Serial.println("[WEB] server stopped");
}
