#include <Arduino.h>
#include <WiFi.h>
#include <ArduinoOTA.h>

// ===== USER WIFI =====
static const char* WIFI_SSID = "Pilgrims_House";
static const char* WIFI_PASS = "PH2020PrV";

// ===== PINS =====
static const int EN1_PIN  = 12;   // PWM -> L293D EN1
static const int IN1_PIN  = 15;   // Direction
static const int IN2_PIN  = 27;   // Direction
static const int SOIL_PIN = 34;   // your working ADC input-only pin

// ===== SD SPI pins =====
// Állítsd ide a VALÓS bekötésedet (és a modul VCC legyen 5V ennél a kék modulnál).
// Ha az "ESP32 default" tesztet akarod: MISO=19 MOSI=23 SCK=18 CS=21 (CS ne legyen 5).
static const int SD_MISO = 19;
static const int SD_MOSI = 23;
static const int SD_SCK  = 18;
static const int SD_CS   = 5;

// ===== PWM =====
static const int PWM_FREQ = 2000;
static const int PWM_RES  = 8;    // 0..255

// ===== FILTER =====
static const int NUM_SAMPLES = 11;      // odd
static const int SAMPLE_DELAY_MS = 6;

// ===== HISTORY =====
static const int HIST_LEN = 180;
static const unsigned long HIST_PERIOD_MS = 1000;
static const unsigned long HISTORY_FLUSH_MS = 10000; // save history.bin every 10s

// ===== CPU usage estimate =====
static unsigned long busyAccumUs = 0;
static unsigned long sampleStartMs = 0;
static const unsigned long CPU_SAMPLE_MS = 1000;

// ===== Pump modes =====
enum PumpMode : uint8_t { PUMP_OFF = 0, PUMP_AUTO = 1, PUMP_ON = 2 };

// ===== Config =====
struct Config {
  int dryOn;                 // >= -> ON
  int wetOff;                // <= -> OFF
  int pumpPwm;               // 0..255
  unsigned long minOnMs;
  unsigned long minOffMs;

  unsigned long limitWindowSec;
  unsigned long maxOnSecInWindow;

  bool softRamp;
  int rampStep;
  int rampDelayMs;

  unsigned long soilLogPeriodMs;
  PumpMode mode;
};

// ===== Runtime =====
struct Runtime {
  int soilNow = 0;
  float chipTempC = NAN;
  float cpuUsagePct = 0.0f;

  bool pumpOn = false;
  bool lockout = false;

  unsigned long windowStartMs = 0;
  unsigned long onTimeThisWindowMs = 0;

  IPAddress ip;
};

// ===== Histories =====
struct Histories {
  int soil[HIST_LEN]{};
  int16_t tempC_x10[HIST_LEN]{};
  uint8_t cpuPct[HIST_LEN]{};

  uint16_t idx = 0;
  bool filled = false;
};

// ===== Globals =====
static Config cfg;
static Runtime rt;
static Histories hist;

// PWM internal
static int currentPwm = 0;
static unsigned long lastSwitchMs = 0;
static unsigned long lastLoopMs = 0;
static unsigned long lastHistMs = 0;
static unsigned long lastHistoryFlushMs = 0;
static unsigned long lastSoilLogMs = 0;

// ===== Modules =====
#include "storage.h"
#include "web.h"

// ===== Helpers =====
static inline int clampi(int v, int lo, int hi) {
  if (v < lo) return lo;
  if (v > hi) return hi;
  return v;
}

static void setDefaultConfig() {
  cfg.dryOn = 2500;
  cfg.wetOff = 2300;
  cfg.pumpPwm = 180;
  cfg.minOnMs = 4000;
  cfg.minOffMs = 2000;

  cfg.limitWindowSec = 300;
  cfg.maxOnSecInWindow = 60;

  cfg.softRamp = true;
  cfg.rampStep = 10;
  cfg.rampDelayMs = 10;

  cfg.soilLogPeriodMs = 60000;
  cfg.mode = PUMP_AUTO;
}

static void applyConfigSanity() {
  cfg.pumpPwm = clampi(cfg.pumpPwm, 0, 255);
  if (cfg.wetOff >= cfg.dryOn) cfg.wetOff = cfg.dryOn - 50;
  if (cfg.limitWindowSec < 5) cfg.limitWindowSec = 5;
  if (cfg.rampStep < 1) cfg.rampStep = 1;
  if ((long)cfg.rampDelayMs < 0) cfg.rampDelayMs = 0;
  if (cfg.soilLogPeriodMs < 5000) cfg.soilLogPeriodMs = 5000;
  if (cfg.mode > PUMP_ON) cfg.mode = PUMP_AUTO;
}

static int readSoilFiltered() {
  int v[NUM_SAMPLES];
  for (int i = 0; i < NUM_SAMPLES; i++) {
    v[i] = analogRead(SOIL_PIN);
    delay(SAMPLE_DELAY_MS);
  }
  for (int i = 0; i < NUM_SAMPLES; i++) {
    for (int j = i + 1; j < NUM_SAMPLES; j++) {
      if (v[j] < v[i]) { int t=v[i]; v[i]=v[j]; v[j]=t; }
    }
  }
  int mid = NUM_SAMPLES / 2;
  if (NUM_SAMPLES >= 5) return (v[mid-1] + v[mid] + v[mid+1]) / 3;
  return v[mid];
}

static void rampPwmTo(int target) {
  target = clampi(target, 0, 255);

  if (!cfg.softRamp) {
    currentPwm = target;
    ledcWrite(EN1_PIN, currentPwm);
    return;
  }

  while (currentPwm != target) {
    if (currentPwm < target) currentPwm = min(currentPwm + cfg.rampStep, target);
    else                     currentPwm = max(currentPwm - cfg.rampStep, target);
    ledcWrite(EN1_PIN, currentPwm);
    delay(cfg.rampDelayMs);
  }
}

static void pumpSet(bool on) {
  rampPwmTo(on ? cfg.pumpPwm : 0);
}

static void resetWindowIfNeeded(unsigned long nowMs) {
  const unsigned long winMs = cfg.limitWindowSec * 1000UL;
  if (nowMs - rt.windowStartMs >= winMs) {
    rt.windowStartMs = nowMs;
    rt.onTimeThisWindowMs = 0;
    rt.lockout = false;
  }
}

static float readChipTempC() {
#if defined(ARDUINO_ARCH_ESP32)
  return temperatureRead(); // approximate
#else
  return NAN;
#endif
}

static void historiesPush(int soil, float tempC, float cpuPct) {
  hist.soil[hist.idx] = soil;

  if (isnan(tempC)) hist.tempC_x10[hist.idx] = INT16_MIN;
  else hist.tempC_x10[hist.idx] = (int16_t)clampi((int)lroundf(tempC * 10.0f), -32768, 32767);

  int cpuI = (int)lroundf(cpuPct);
  hist.cpuPct[hist.idx] = (uint8_t)clampi(cpuI, 0, 100);

  hist.idx++;
  if (hist.idx >= HIST_LEN) {
    hist.idx = 0;
    hist.filled = true;
  }
}

static void updateCpuUsageEstimate(unsigned long loopStartUs, unsigned long loopEndUs) {
  const unsigned long busyUs = (loopEndUs - loopStartUs);
  busyAccumUs += busyUs;

  const unsigned long nowMs = millis();
  if (nowMs - sampleStartMs >= CPU_SAMPLE_MS) {
    const unsigned long windowUs = (nowMs - sampleStartMs) * 1000UL;
    float usage = 0.0f;
    if (windowUs > 0) usage = (100.0f * (float)busyAccumUs) / (float)windowUs;
    if (usage < 0) usage = 0;
    if (usage > 100) usage = 100;
    rt.cpuUsagePct = usage;

    busyAccumUs = 0;
    sampleStartMs = nowMs;
  }
}

static void controlPump(unsigned long nowMs, unsigned long dtMs) {
  if (rt.pumpOn && dtMs < 2000) rt.onTimeThisWindowMs += dtMs;

  if (!rt.lockout && cfg.maxOnSecInWindow > 0) {
    if (rt.onTimeThisWindowMs >= cfg.maxOnSecInWindow * 1000UL) {
      rt.lockout = true;
      if (rt.pumpOn) {
        rt.pumpOn = false;
        lastSwitchMs = nowMs;
        pumpSet(false);
        storage_logEvent("LOCKOUT", "rate_limit_exceeded");
      }
    }
  }

  if (cfg.mode == PUMP_OFF) {
    if (rt.pumpOn) {
      rt.pumpOn = false;
      lastSwitchMs = nowMs;
      pumpSet(false);
      storage_logEvent("PUMP", "forced_off");
    }
    return;
  }

  if (cfg.mode == PUMP_ON) {
    if (!rt.lockout) {
      if (!rt.pumpOn) {
        rt.pumpOn = true;
        lastSwitchMs = nowMs;
        pumpSet(true);
        storage_logEvent("PUMP", "forced_on");
      }
    }
    return;
  }

  // AUTO
  if (!rt.pumpOn) {
    if (!rt.lockout &&
        rt.soilNow >= cfg.dryOn &&
        (nowMs - lastSwitchMs) >= cfg.minOffMs) {
      rt.pumpOn = true;
      lastSwitchMs = nowMs;
      pumpSet(true);
      storage_logEvent("PUMP", "auto_on");
    }
  } else {
    if (rt.soilNow <= cfg.wetOff &&
        (nowMs - lastSwitchMs) >= cfg.minOnMs) {
      rt.pumpOn = false;
      lastSwitchMs = nowMs;
      pumpSet(false);
      storage_logEvent("PUMP", "auto_off");
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(1500);
  Serial.println("BOOT: reached setup()");

  // ===== WiFi FIRST =====
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  Serial.print("WiFi connecting");
  while (WiFi.status() != WL_CONNECTED) {
    delay(200);
    Serial.print(".");
  }
  Serial.println();

  IPAddress ip = WiFi.localIP();
  Serial.print("IP: ");
  Serial.println(ip);

  // ===== OTA =====
  ArduinoOTA.setHostname("esp32-irrigation");

  ArduinoOTA.onStart([]() {
    Serial.println("[OTA] Start");
    storage_logEvent("OTA", "start");
  });

  ArduinoOTA.onEnd([]() {
    Serial.println("[OTA] End");
    storage_logEvent("OTA", "end");
  });

  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    static unsigned int last = 0;
    unsigned int p = (progress * 100) / total;
    if (p != last) {
      Serial.printf("[OTA] Progress: %u%%\n", p);
      last = p;
    }
  });

  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("[OTA] Error %u\n", error);
    storage_logEvent("OTA", "error");
  });

  ArduinoOTA.begin();
  Serial.println("[OTA] Ready");

  // ===== HARDWARE INIT =====
  pinMode(IN1_PIN, OUTPUT);
  pinMode(IN2_PIN, OUTPUT);
  digitalWrite(IN1_PIN, HIGH);
  digitalWrite(IN2_PIN, LOW);

  ledcAttach(EN1_PIN, PWM_FREQ, PWM_RES);
  ledcWrite(EN1_PIN, 0);

  setDefaultConfig();

  // ===== SD INIT AFTER WIFI + OTA =====
  storage_begin(SD_CS, SD_SCK, SD_MISO, SD_MOSI);
  if (storage_isReady()) {
    storage_ensureFiles();

    if (storage_loadConfig(cfg)) {
      applyConfigSanity();
      storage_logEvent("BOOT", "config_loaded");
    } else {
      applyConfigSanity();
      storage_saveConfig(cfg);
      storage_logEvent("BOOT", "config_default_saved");
    }

    storage_loadHistory(hist);
  }

  // ===== WEB =====
  web_begin(&cfg, &rt, &hist);
  Serial.println("[WEB] server started");
}

void loop() {
  ArduinoOTA.handle();

  const unsigned long loopStartUs = micros();

  web_handleClient();

  const unsigned long nowMs = millis();
  const unsigned long dtMs = nowMs - lastLoopMs;
  lastLoopMs = nowMs;

  resetWindowIfNeeded(nowMs);

  rt.soilNow = readSoilFiltered();
  rt.chipTempC = readChipTempC();

  controlPump(nowMs, dtMs);

  // histories @ 1Hz
  if (nowMs - lastHistMs >= HIST_PERIOD_MS) {
    lastHistMs = nowMs;

    historiesPush(rt.soilNow, rt.chipTempC, rt.cpuUsagePct);

    // long-term SD logging period
    if (storage_isReady() && (nowMs - lastSoilLogMs >= cfg.soilLogPeriodMs)) {
      lastSoilLogMs = nowMs;
      storage_logSoilPoint(rt, cfg);
    }
  }

  // persist history for reboot-proof UI
  if (storage_isReady() && (nowMs - lastHistoryFlushMs >= HISTORY_FLUSH_MS)) {
    lastHistoryFlushMs = nowMs;
    storage_saveHistory(hist);
  }

  const unsigned long loopEndUs = micros();
  updateCpuUsageEstimate(loopStartUs, loopEndUs);

  delay(10);
}
