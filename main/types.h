#pragma once
#include <Arduino.h>

// ===== CONSTANTS =====
static const int HIST_LEN = 180;

// ===== ENUMS =====
enum PumpMode : uint8_t {
  PUMP_OFF  = 0,
  PUMP_AUTO = 1,
  PUMP_ON   = 2
};

// ===== CONFIG =====
struct Config {
  int dryOn = 2500;
  int wetOff = 2300;
  int pumpPwm = 180;

  unsigned long minOnMs = 4000;
  unsigned long minOffMs = 2000;

  unsigned long limitWindowSec = 300;
  unsigned long maxOnSecInWindow = 60;

  bool softRamp = true;
  unsigned long soilLogPeriodMs = 60000;

  PumpMode mode = PUMP_AUTO;
};

// ===== RUNTIME =====
struct Runtime {
  int soilNow = 0;
  float chipTempC = NAN;
  float cpuUsagePct = 0;

  bool pumpOn = false;
  bool lockout = false;

  unsigned long windowStartMs = 0;
  unsigned long onTimeThisWindowMs = 0;
};

// ===== HISTORY =====
struct Histories {
  int16_t soil[HIST_LEN]{};
  int16_t tempC_x10[HIST_LEN]{};
  uint8_t cpuPct[HIST_LEN]{};

  uint16_t idx = 0;
  bool filled = false;
};
