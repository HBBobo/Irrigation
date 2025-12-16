#include <esp_task_wdt.h>
#include "config.h"
#include "credentials.h"
#include "net.h"
#include "ota.h"
#include "storage.h"
#include "web.h"

// Hardware pins
#define SOIL_PIN     34   // ADC input for soil moisture sensor
#define EN1_PIN      13   // Motor driver enable (PWM)
#define IN1_PIN      15   // Motor driver input 1
#define IN2_PIN      27   // Motor driver input 2

// PWM settings
#define PWM_CHANNEL  0
#define PWM_FREQ     5000
#define PWM_RES      8    // 8-bit resolution (0-255)

Config cfg;
Runtime rt;
Histories hist;

// ---- Sensor reading ----
static void readSensors() {
  // Read soil moisture (ADC)
  rt.soilNow = analogRead(SOIL_PIN);

  // Read internal temperature sensor
  rt.tempC_x10 = (int16_t)(temperatureRead() * 10);

  // CPU usage tracking (simplified - percentage of loop time)
  static uint32_t loopCount = 0;
  static uint32_t lastCpuCalc = 0;
  loopCount++;

  if (millis() - lastCpuCalc >= 1000) {
    // Rough estimate: assume 1000 loops/sec is 100% utilization
    rt.cpuPct = min((uint8_t)100, (uint8_t)(loopCount / 10));
    loopCount = 0;
    lastCpuCalc = millis();
  }
}

// ---- Pump control with hysteresis and safety limits ----
static void controlPump() {
  uint32_t now = millis();

  // Reset window if expired
  if (now - rt.windowStartMs >= cfg.limitWindowSec * 1000UL) {
    rt.windowStartMs = now;
    rt.onTimeThisWindowMs = 0;
    rt.lockout = false;
  }

  bool shouldBeOn = false;

  // Determine desired pump state based on mode
  if (cfg.mode == PUMP_ON) {
    shouldBeOn = true;
  } else if (cfg.mode == PUMP_AUTO) {
    // Hysteresis logic
    if (rt.pumpOn) {
      // Pump is ON: stay on until soil is wet enough
      shouldBeOn = (rt.soilNow >= cfg.wetOff);
    } else {
      // Pump is OFF: turn on only when soil is dry enough
      shouldBeOn = (rt.soilNow >= cfg.dryOn);
    }
  }
  // PUMP_OFF mode: shouldBeOn stays false

  // Apply safety limits
  if (shouldBeOn) {
    // Check max on-time in window
    if (rt.onTimeThisWindowMs >= cfg.maxOnSecInWindow * 1000UL) {
      shouldBeOn = false;
      rt.lockout = true;
    }
    // Check min off time (prevent turning on too soon after turning off)
    if (!rt.pumpOn && (now - rt.lastPumpChangeMs < cfg.minOffMs)) {
      shouldBeOn = false;
    }
  } else {
    // Check min on time (prevent turning off too soon after turning on)
    if (rt.pumpOn && (now - rt.lastPumpChangeMs < cfg.minOnMs)) {
      shouldBeOn = true;
    }
  }

  // Apply pump state change
  if (shouldBeOn && !rt.pumpOn) {
    // Turn pump ON (forward direction)
    rt.pumpOn = true;
    rt.lastPumpChangeMs = now;

    digitalWrite(IN1_PIN, HIGH);
    digitalWrite(IN2_PIN, LOW);

    if (cfg.softRamp) {
      // Soft ramp-up
      for (int pwm = 0; pwm <= cfg.pumpPwm; pwm += 10) {
        ledcWrite(EN1_PIN, pwm);
        delay(20);
      }
    }
    ledcWrite(EN1_PIN, cfg.pumpPwm);
    Serial.println("[PUMP] ON");

  } else if (!shouldBeOn && rt.pumpOn) {
    // Turn pump OFF
    rt.pumpOn = false;
    rt.lastPumpChangeMs = now;
    ledcWrite(EN1_PIN, 0);
    digitalWrite(IN1_PIN, LOW);
    digitalWrite(IN2_PIN, LOW);
    Serial.println("[PUMP] OFF");
  }

  // Track on-time within window
  static uint32_t lastLoopMs = 0;
  if (rt.pumpOn && lastLoopMs > 0) {
    rt.onTimeThisWindowMs += (now - lastLoopMs);
  }
  lastLoopMs = now;
}

// ---- History and logging ----
static void updateHistoryAndLog() {
  uint32_t now = millis();

  if (now - rt.lastLogMs < cfg.logPeriodMs) return;
  rt.lastLogMs = now;

  // Update circular buffer
  hist.soil[hist.idx] = rt.soilNow;
  hist.tempC_x10[hist.idx] = rt.tempC_x10;
  hist.cpuPct[hist.idx] = rt.cpuPct;

  hist.idx = (hist.idx + 1) % HIST_LEN;
  if (hist.idx == 0) hist.filled = true;

  // Append to log file
  storage_appendLog(rt);

  // Periodically save history to SD (every 10 log entries)
  static uint8_t saveCounter = 0;
  if (++saveCounter >= 10) {
    saveCounter = 0;
    storage_saveHistory(hist);
  }
}

void setup() {
  Serial.begin(115200);
  Serial.println("BOOT");

  // Watchdog will be enabled after setup completes

  // Initialize motor driver pins
  pinMode(IN1_PIN, OUTPUT);
  pinMode(IN2_PIN, OUTPUT);
  digitalWrite(IN1_PIN, LOW);
  digitalWrite(IN2_PIN, LOW);

  // Initialize PWM for motor enable (ESP-IDF v5.x API)
  ledcAttach(EN1_PIN, PWM_FREQ, PWM_RES);
  ledcWrite(EN1_PIN, 0);  // Ensure pump is off

  // Initialize ADC
  analogReadResolution(12);  // 12-bit ADC (0-4095)

  // Connect to WiFi
  net_begin(WIFI_SSID, WIFI_PASS);

  // Initialize SD card and load config/history
  storage_begin(5, 18, 19, 23);
  storage_loadConfig(cfg);
  storage_validateConfig(cfg);
  storage_loadHistory(hist);
  storage_ensureWebUI(net_isUp());

  if (net_isUp()) {
    ota_begin();
    web_begin(&cfg, &rt, &hist);
  }

  rt.windowStartMs = millis();
  rt.lastLogMs = millis();

  // Re-enable watchdog now that setup is complete
  esp_task_wdt_add(NULL);

  Serial.println("[SYS] ready");
}

void loop() {
  // Feed watchdog
  esp_task_wdt_reset();

  // Core functionality
  readSensors();
  controlPump();
  updateHistoryAndLog();

  // Network services
  if (net_isUp()) {
    web_loop();
    ota_loop();
  }

  // Small delay to prevent tight loop
  delay(10);
}
