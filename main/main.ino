#include <Arduino.h>

#include "config.h"
#include "net.h"
#include "ota.h"
#include "storage.h"
#include "web.h"

// ===== YOUR WIFI (as requested earlier)
const char* WIFI_SSID = "Pilgrims_House";
const char* WIFI_PASS = "PH2020PrV";

// ===== VERSION
const char* FW_VERSION = "1.0.1";

// ===== PINS
static const int EN1_PIN  = 12;  // PWM enable (L293D EN)
static const int IN1_PIN  = 15;
static const int IN2_PIN  = 27;
static const int SOIL_PIN = 34;

// SD SPI pins (your current working/default set)
static const int SD_MISO = 19;
static const int SD_MOSI = 23;
static const int SD_SCK  = 18;
static const int SD_CS   = 5;

// PWM
static const int PWM_FREQ = 2000;
static const int PWM_RES  = 8;

// state
static Config cfg;
static Runtime rt;
static Histories hist;

static bool wifiReady = false;
static bool servicesStarted = false;

static int readSoilMedian7() {
  int v[7];
  for (int i = 0; i < 7; i++) {
    v[i] = analogRead(SOIL_PIN);
    delay(2);
  }
  for (int i = 0; i < 7; i++)
    for (int j = i + 1; j < 7; j++)
      if (v[j] < v[i]) { int t = v[i]; v[i] = v[j]; v[j] = t; }
  return v[3];
}

static void pumpWrite(int pwm) {
  if (pwm < 0) pwm = 0;
  if (pwm > 255) pwm = 255;
  ledcWrite(EN1_PIN, pwm);
}

static void pumpSet(bool on) {
  if (!on) {
    pumpWrite(0);
    return;
  }

  if (!cfg.softRamp) {
    pumpWrite(cfg.pumpPwm);
    return;
  }

  // soft ramp (fast)
  for (int p = 0; p <= cfg.pumpPwm; p += 25) {
    pumpWrite(p);
    delay(10);
    delay(0);
  }
  pumpWrite(cfg.pumpPwm);
}

static void historyPush() {
  hist.soil[hist.idx] = (int16_t)rt.soilNow;
  hist.tempC_x10[hist.idx] = rt.tempC_x10;
  hist.cpuPct[hist.idx] = rt.cpuPct;

  hist.idx++;
  if (hist.idx >= HIST_LEN) {
    hist.idx = 0;
    hist.filled = true;
  }
}

// simple CPU “usage” estimate: busy vs elapsed in the 1s bucket
static void cpuTick() {
  static uint32_t lastMs = 0;
  static uint32_t busyAcc = 0;
  static uint32_t startMs = 0;

  uint32_t now = millis();
  if (startMs == 0) { startMs = now; lastMs = now; }

  // in Arduino loop, we can approximate busy as (time since last tick minus the explicit idle)
  // we’ll treat delay(0) as “idle slice”, so we mainly get a rough trend.
  uint32_t dt = now - lastMs;
  busyAcc += dt;
  lastMs = now;

  if (now - startMs >= 1000) {
    uint32_t total = now - startMs;
    uint32_t pct = (total == 0) ? 0 : (busyAcc * 100UL) / total;
    if (pct > 100) pct = 100;
    rt.cpuPct = (uint8_t)pct;
    busyAcc = 0;
    startMs = now;
  }
}

static void updateSensors() {
  rt.soilNow = readSoilMedian7();

  // chip temp (Arduino-ESP32 has temperatureRead())
  float tC = temperatureRead();
  rt.tempC_x10 = (int16_t)(tC * 10.0f);
}

static void pumpLogic(uint32_t now) {
  // window accounting
  if (rt.windowStartMs == 0) rt.windowStartMs = now;
  uint32_t windowMs = cfg.limitWindowSec * 1000UL;
  if (now - rt.windowStartMs >= windowMs) {
    rt.windowStartMs = now;
    rt.onTimeThisWindowMs = 0;
    rt.lockout = false;
  }

  // update ON time while running
  static uint32_t lastOnUpdate = 0;
  if (lastOnUpdate == 0) lastOnUpdate = now;

  if (rt.pumpOn) {
    rt.onTimeThisWindowMs += (now - lastOnUpdate);
    uint32_t maxOnMs = cfg.maxOnSecInWindow * 1000UL;
    if (rt.onTimeThisWindowMs >= maxOnMs) {
      rt.lockout = true;
    }
  }
  lastOnUpdate = now;

  // mode control
  bool wantOn = false;

  if (cfg.mode == PUMP_OFF) wantOn = false;
  else if (cfg.mode == PUMP_ON) wantOn = true;
  else {
    // AUTO hysteresis
    if (!rt.pumpOn && rt.soilNow >= cfg.dryOn) wantOn = true;
    if (rt.pumpOn && rt.soilNow <= cfg.wetOff) wantOn = false;
    if (rt.pumpOn) wantOn = true; // keep until wetOff hit (above handled)
  }

  // lockout overrides
  if (rt.lockout) wantOn = false;

  // min on/off
  uint32_t sinceChange = now - rt.lastPumpChangeMs;
  if (rt.pumpOn && !wantOn && sinceChange < cfg.minOnMs) wantOn = true;
  if (!rt.pumpOn && wantOn && sinceChange < cfg.minOffMs) wantOn = false;

  // apply changes
  if (wantOn != rt.pumpOn) {
    rt.pumpOn = wantOn;
    rt.lastPumpChangeMs = now;
    pumpSet(rt.pumpOn);
  } else {
    // keep PWM if on
    if (rt.pumpOn) pumpWrite(cfg.pumpPwm);
  }
}

void setup() {
  Serial.begin(115200);
  delay(1200);

  Serial.println("BOOT");

  // pins
  pinMode(IN1_PIN, OUTPUT);
  pinMode(IN2_PIN, OUTPUT);
  digitalWrite(IN1_PIN, HIGH);
  digitalWrite(IN2_PIN, LOW);

  ledcAttach(EN1_PIN, PWM_FREQ, PWM_RES);
  pumpWrite(0);

  // start WiFi ONCE (background connect)
  net_begin(WIFI_SSID, WIFI_PASS);

  // SD
  storage_begin(SD_CS, SD_SCK, SD_MISO, SD_MOSI);
  if (storage_isReady()) {
    storage_mkdirs();
    storage_loadConfig(cfg);
    storage_loadHistory(hist);
  }

  Serial.println("[SYS] ready");
}

void loop() {
  uint32_t now = millis();

  // WiFi “ready edge”: start OTA + Web once when connected
  if (!servicesStarted && net_isUp()) {
    servicesStarted = true;
    Serial.println("[NET] CONNECTED");
    Serial.print("[NET] IP: ");
    Serial.println(net_ip());
    Serial.print("[NET] RSSI: ");
    Serial.println(WiFi.RSSI());

    ota_begin();

    // ensure web UI cached from GH if missing
    storage_ensureWebUI(true);

    web_begin(&cfg, &rt, &hist);
  }

  // always keep CPU fed
  cpuTick();

  // sensors + logic
  updateSensors();
  pumpLogic(now);

  // logging/history
  if (now - rt.lastLogMs >= cfg.logPeriodMs) {
    rt.lastLogMs = now;
    historyPush();
    storage_saveHistory(hist);
    storage_appendLog(rt);
  }

  // services
  if (servicesStarted) {
    web_loop();
    ota_loop();
  }

  delay(0);
}
