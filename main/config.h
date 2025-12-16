#pragma once
#include <Arduino.h>

#define HIST_LEN 240  // ~240 minta (ha 10s log: 40 perc)

enum PumpMode : uint8_t {
  PUMP_OFF  = 0,
  PUMP_AUTO = 1,
  PUMP_ON   = 2
};

struct Config {
  // thresholds (hysteresis!)
  int dryOn  = 2500;  // soil >= dryOn => pump ON
  int wetOff = 2200;  // soil <= wetOff => pump OFF

  // pump behavior
  int pumpPwm = 180;     // 0..255
  bool softRamp = true;  // ramp-up to PWM

  // minimum on/off times to prevent chattering
  uint32_t minOnMs  = 5000;
  uint32_t minOffMs = 5000;

  // safety limit: within windowSec, pump can be ON at most maxOnSec
  uint32_t limitWindowSec     = 600; // 10 min
  uint32_t maxOnSecInWindow   = 60;  // max 60s ON in 10 min

  // logging
  uint32_t logPeriodMs = 10000;

  // mode
  PumpMode mode = PUMP_AUTO;
};

struct Runtime {
  int soilNow = 0;
  int16_t tempC_x10 = 0;
  uint8_t cpuPct = 0;

  bool pumpOn = false;
  bool lockout = false;

  uint32_t lastPumpChangeMs = 0;
  uint32_t windowStartMs = 0;
  uint32_t onTimeThisWindowMs = 0;

  uint32_t lastLogMs = 0;
  uint32_t lastCpuMs = 0;
};

struct Histories {
  int16_t soil[HIST_LEN];
  int16_t tempC_x10[HIST_LEN];
  uint8_t cpuPct[HIST_LEN];
  uint16_t idx = 0;
  bool filled = false;
};
