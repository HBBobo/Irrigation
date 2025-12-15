#pragma once
#include <Arduino.h>
#include <SPI.h>
#include <SdFat.h>

// ================= SD / FS =================
SdFat sd;          // handles FAT32 + exFAT
FsFile file;

static bool sdReady = false;

// ================= FILE PATHS =================
static const char* CFG_PATH    = "/config.txt";     // egyszerű key=value
static const char* SOIL_LOG    = "/soil.csv";
static const char* EVENTS_LOG  = "/events.csv";
static const char* HISTORY_BIN = "/history.bin";

// ================= HISTORY =================
static const uint32_t HISTORY_MAGIC = 0xB0B0B0B0;

struct HistoryBlob {
  uint32_t magic;
  uint16_t len;
  uint16_t idx;
  uint8_t  filled;
  uint8_t  reserved[3];
  int16_t  soil[HIST_LEN];
  int16_t  tempC_x10[HIST_LEN];
  uint8_t  cpuPct[HIST_LEN];
};

// ================= INIT =================
static bool storage_begin(int cs, int sck, int miso, int mosi) {
  SPI.begin(sck, miso, mosi, cs);

  Serial.println("[SD] SdFat init...");

  SdSpiConfig cfg(
    cs,
    DEDICATED_SPI,
    SD_SCK_MHZ(4)      // 🔴 fontos: lassú órajel
  );

  if (!sd.begin(cfg)) {
    Serial.println("[SD] init FAILED");
    sdReady = false;
    return false;
  }

  sdReady = true;
  Serial.println("[SD] init OK (SdFat)");
  return true;
}

static bool storage_isReady() {
  return sdReady;
}

// ================= FILE BOOTSTRAP =================
static void storage_ensureFiles() {
  if (!sdReady) return;

  if (!sd.exists(SOIL_LOG)) {
    file.open(SOIL_LOG, O_WRITE | O_CREAT);
    file.println("ts,soil_adc,pump_on,lockout,on_time_window_ms");
    file.close();
  }

  if (!sd.exists(EVENTS_LOG)) {
    file.open(EVENTS_LOG, O_WRITE | O_CREAT);
    file.println("ts,event,detail");
    file.close();
  }
}

// ================= CONFIG =================
// simple key=value format (robust, no JSON parser needed)

static bool storage_loadConfig(Config& cfg) {
  if (!sdReady || !sd.exists(CFG_PATH)) return false;

  file.open(CFG_PATH, O_READ);
  while (file.available()) {
    String line = file.readStringUntil('\n');
    line.trim();
    if (line.length() == 0) continue;

    int eq = line.indexOf('=');
    if (eq < 0) continue;

    String k = line.substring(0, eq);
    String v = line.substring(eq + 1);

    if (k == "dryOn") cfg.dryOn = v.toInt();
    else if (k == "wetOff") cfg.wetOff = v.toInt();
    else if (k == "pumpPwm") cfg.pumpPwm = v.toInt();
    else if (k == "minOnMs") cfg.minOnMs = v.toInt();
    else if (k == "minOffMs") cfg.minOffMs = v.toInt();
    else if (k == "limitWindowSec") cfg.limitWindowSec = v.toInt();
    else if (k == "maxOnSecInWindow") cfg.maxOnSecInWindow = v.toInt();
    else if (k == "soilLogPeriodMs") cfg.soilLogPeriodMs = v.toInt();
    else if (k == "softRamp") cfg.softRamp = (v.toInt() != 0);
    else if (k == "mode") cfg.mode = (PumpMode)v.toInt();
  }
  file.close();
  return true;
}

static bool storage_saveConfig(const Config& cfg) {
  if (!sdReady) return false;

  file.open(CFG_PATH, O_WRITE | O_CREAT | O_TRUNC);
  file.printf("dryOn=%d\n", cfg.dryOn);
  file.printf("wetOff=%d\n", cfg.wetOff);
  file.printf("pumpPwm=%d\n", cfg.pumpPwm);
  file.printf("minOnMs=%lu\n", cfg.minOnMs);
  file.printf("minOffMs=%lu\n", cfg.minOffMs);
  file.printf("limitWindowSec=%lu\n", cfg.limitWindowSec);
  file.printf("maxOnSecInWindow=%lu\n", cfg.maxOnSecInWindow);
  file.printf("soilLogPeriodMs=%lu\n", cfg.soilLogPeriodMs);
  file.printf("softRamp=%d\n", cfg.softRamp ? 1 : 0);
  file.printf("mode=%d\n", (int)cfg.mode);
  file.close();
  return true;
}

// ================= HISTORY =================
static bool storage_saveHistory(const Histories& h) {
  if (!sdReady) return false;

  HistoryBlob hb;
  hb.magic = HISTORY_MAGIC;
  hb.len = HIST_LEN;
  hb.idx = h.idx;
  hb.filled = h.filled ? 1 : 0;
  memset(hb.reserved, 0, sizeof(hb.reserved));

  memcpy(hb.soil, h.soil, sizeof(hb.soil));
  memcpy(hb.tempC_x10, h.tempC_x10, sizeof(hb.tempC_x10));
  memcpy(hb.cpuPct, h.cpuPct, sizeof(hb.cpuPct));

  file.open(HISTORY_BIN, O_WRITE | O_CREAT | O_TRUNC);
  file.write(&hb, sizeof(hb));
  file.close();
  return true;
}

static bool storage_loadHistory(Histories& h) {
  if (!sdReady || !sd.exists(HISTORY_BIN)) return false;

  HistoryBlob hb;
  file.open(HISTORY_BIN, O_READ);
  if (file.read(&hb, sizeof(hb)) != sizeof(hb)) {
    file.close();
    return false;
  }
  file.close();

  if (hb.magic != HISTORY_MAGIC || hb.len != HIST_LEN) return false;

  h.idx = hb.idx;
  h.filled = hb.filled;
  memcpy(h.soil, hb.soil, sizeof(hb.soil));
  memcpy(h.tempC_x10, hb.tempC_x10, sizeof(hb.tempC_x10));
  memcpy(h.cpuPct, hb.cpuPct, sizeof(hb.cpuPct));
  return true;
}

// ================= LOGGING =================
static String storage_nowTs() {
  return String(millis());
}

static void storage_logEvent(const String& event, const String& detail) {
  if (!sdReady) return;
  file.open(EVENTS_LOG, O_WRITE | O_CREAT | O_APPEND);
  file.print(storage_nowTs());
  file.print(",");
  file.print(event);
  file.print(",");
  file.println(detail);
  file.close();
}

static void storage_logSoilPoint(const Runtime& rt, const Config&) {
  if (!sdReady) return;
  file.open(SOIL_LOG, O_WRITE | O_CREAT | O_APPEND);
  file.print(storage_nowTs());
  file.print(",");
  file.print(rt.soilNow);
  file.print(",");
  file.print(rt.pumpOn ? 1 : 0);
  file.print(",");
  file.print(rt.lockout ? 1 : 0);
  file.print(",");
  file.println(rt.onTimeThisWindowMs);
  file.close();
}
