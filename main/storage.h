#pragma once
#include <Arduino.h>
#include <SdFat.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

#include "config.h"

extern const char* FW_VERSION;

static SdFat sd;
static bool g_sdReady = false;

// ---- paths
static const char* PATH_CFG  = "/cfg.txt";
static const char* PATH_HIST = "/hist.bin";
static const char* PATH_LOG  = "/log.csv";

// Web UI on SD
static const char* WEB_DIR = "/web";
static const char* WEB_INDEX = "/web/index.html";

// GitHub raw base (UI files)
static const char* GH_WEB_BASE =
  "https://raw.githubusercontent.com/HBBobo/Irrigation/webui"; // <-- tedd ide a webui fájlokat

// ---- history blob
static const uint32_t HISTORY_MAGIC = 0xB0B0B0B0;

struct HistoryBlob {
  uint32_t magic;
  uint16_t len;
  uint16_t idx;
  uint8_t  filled;
  uint8_t  rsv[3];
  int16_t  soil[HIST_LEN];
  int16_t  tempC_x10[HIST_LEN];
  uint8_t  cpuPct[HIST_LEN];
};

static bool storage_isReady() { return g_sdReady; }

static bool storage_begin(int cs, int sck, int miso, int mosi) {
  (void)sck; (void)miso; (void)mosi;
  SdSpiConfig cfg(cs, DEDICATED_SPI, SD_SCK_MHZ(8));
  Serial.println("[SD] SdFat init...");
  g_sdReady = sd.begin(cfg);
  if (g_sdReady) Serial.println("[SD] init OK (SdFat)");
  else Serial.println("[SD] init FAIL");
  return g_sdReady;
}

static void storage_mkdirs() {
  if (!g_sdReady) return;
  if (!sd.exists(WEB_DIR)) sd.mkdir(WEB_DIR);
}

static bool storage_saveConfig(const Config& cfg) {
  if (!g_sdReady) return false;
  FsFile f = sd.open(PATH_CFG, O_WRITE | O_CREAT | O_TRUNC);
  if (!f) return false;

  f.printf("dryOn=%d\n", cfg.dryOn);
  f.printf("wetOff=%d\n", cfg.wetOff);
  f.printf("pumpPwm=%d\n", cfg.pumpPwm);
  f.printf("softRamp=%d\n", cfg.softRamp ? 1 : 0);
  f.printf("minOnMs=%lu\n", (unsigned long)cfg.minOnMs);
  f.printf("minOffMs=%lu\n", (unsigned long)cfg.minOffMs);
  f.printf("limitWindowSec=%lu\n", (unsigned long)cfg.limitWindowSec);
  f.printf("maxOnSecInWindow=%lu\n", (unsigned long)cfg.maxOnSecInWindow);
  f.printf("logPeriodMs=%lu\n", (unsigned long)cfg.logPeriodMs);
  f.printf("mode=%d\n", (int)cfg.mode);

  f.close();
  return true;
}

static bool storage_loadConfig(Config& cfg) {
  if (!g_sdReady) return false;
  FsFile f = sd.open(PATH_CFG, O_RDONLY);
  if (!f) return false;

  String line;
  while (f.available()) {
    line = f.readStringUntil('\n');
    line.trim();
    if (!line.length()) continue;
    int eq = line.indexOf('=');
    if (eq < 0) continue;
    String k = line.substring(0, eq);
    String v = line.substring(eq + 1);

    long iv = v.toInt();
    if (k == "dryOn") cfg.dryOn = (int)iv;
    else if (k == "wetOff") cfg.wetOff = (int)iv;
    else if (k == "pumpPwm") cfg.pumpPwm = (int)iv;
    else if (k == "softRamp") cfg.softRamp = (iv != 0);
    else if (k == "minOnMs") cfg.minOnMs = (uint32_t)iv;
    else if (k == "minOffMs") cfg.minOffMs = (uint32_t)iv;
    else if (k == "limitWindowSec") cfg.limitWindowSec = (uint32_t)iv;
    else if (k == "maxOnSecInWindow") cfg.maxOnSecInWindow = (uint32_t)iv;
    else if (k == "logPeriodMs") cfg.logPeriodMs = (uint32_t)iv;
    else if (k == "mode") cfg.mode = (PumpMode)iv;
  }

  f.close();
  return true;
}

static bool storage_saveHistory(const Histories& h) {
  if (!g_sdReady) return false;

  HistoryBlob hb{};
  hb.magic = HISTORY_MAGIC;
  hb.len = HIST_LEN;
  hb.idx = h.idx;
  hb.filled = h.filled ? 1 : 0;
  memcpy(hb.soil, h.soil, sizeof(hb.soil));
  memcpy(hb.tempC_x10, h.tempC_x10, sizeof(hb.tempC_x10));
  memcpy(hb.cpuPct, h.cpuPct, sizeof(hb.cpuPct));

  FsFile f = sd.open(PATH_HIST, O_WRITE | O_CREAT | O_TRUNC);
  if (!f) return false;
  size_t n = f.write(&hb, sizeof(hb));
  f.close();
  return n == sizeof(hb);
}

static bool storage_loadHistory(Histories& h) {
  if (!g_sdReady) return false;
  FsFile f = sd.open(PATH_HIST, O_RDONLY);
  if (!f) return false;

  HistoryBlob hb{};
  if (f.read(&hb, sizeof(hb)) != sizeof(hb)) { f.close(); return false; }
  f.close();

  if (hb.magic != HISTORY_MAGIC || hb.len != HIST_LEN) return false;

  h.idx = hb.idx;
  h.filled = hb.filled != 0;
  memcpy(h.soil, hb.soil, sizeof(h.soil));
  memcpy(h.tempC_x10, hb.tempC_x10, sizeof(h.tempC_x10));
  memcpy(h.cpuPct, hb.cpuPct, sizeof(h.cpuPct));
  return true;
}

static void storage_appendLog(const Runtime& rt) {
  if (!g_sdReady) return;

  bool exists = sd.exists(PATH_LOG);
  FsFile f = sd.open(PATH_LOG, O_WRITE | O_CREAT | O_APPEND);
  if (!f) return;

  if (!exists) {
    f.println("ms,soil,tempC_x10,cpuPct,pumpOn,lockout,onTimeWindowMs");
  }

  f.printf("%lu,%d,%d,%u,%d,%d,%lu\n",
           (unsigned long)millis(),
           rt.soilNow,
           (int)rt.tempC_x10,
           (unsigned)rt.cpuPct,
           rt.pumpOn ? 1 : 0,
           rt.lockout ? 1 : 0,
           (unsigned long)rt.onTimeThisWindowMs);

  f.close();
}

// ----- GitHub web UI cache: download /web/index.html if missing
static bool storage_downloadToFile(const String& url, const char* outPath) {
  if (!g_sdReady) return false;

  WiFiClientSecure client;
  client.setInsecure(); // simplest; later we can pin cert

  HTTPClient http;
  if (!http.begin(client, url)) return false;

  int code = http.GET();
  if (code != 200) { http.end(); return false; }

  FsFile f = sd.open(outPath, O_WRITE | O_CREAT | O_TRUNC);
  if (!f) { http.end(); return false; }

  WiFiClient* stream = http.getStreamPtr();
  uint8_t buf[1024];
  int total = 0;

  while (http.connected()) {
    int avail = stream->available();
    if (avail > 0) {
      int n = stream->readBytes((char*)buf, (size_t)min(avail, (int)sizeof(buf)));
      if (n <= 0) break;
      f.write(buf, n);
      total += n;
      delay(0);
    } else {
      if (!stream->connected()) break;
      delay(10);
    }
  }

  f.close();
  http.end();
  return total > 0;
}

static void storage_ensureWebUI(bool wifiUp) {
  if (!g_sdReady) return;
  storage_mkdirs();

  if (sd.exists(WEB_INDEX)) return;

  Serial.println("[SD] web files missing");

  if (!wifiUp) {
    Serial.println("[SD] cannot download web UI (no WiFi)");
    return;
  }

  // minimal: only index.html for now
  String url = String(GH_WEB_BASE) + "/index.html";
  Serial.print("[SD] downloading ");
  Serial.println(url);

  if (storage_downloadToFile(url, WEB_INDEX)) {
    Serial.println("[SD] web UI downloaded OK");
  } else {
    Serial.println("[SD] web UI download FAIL");
  }
}
