#pragma once
#include <Arduino.h>
#include <WebServer.h>
#include <WiFi.h>

// Uses globals/types from main.ino:
static WebServer webServer(80);

static Config* gCfg = nullptr;
static Runtime* gRt = nullptr;
static Histories* gHist = nullptr;

// ------- Small helpers -------
static String jsonBool(bool b) { return b ? "true" : "false"; }

static String buildHistoryArray_int(const int* arr, uint16_t idx, bool filled, int len) {
  // returns JSON array with len elements, in chronological order
  String out = "[";
  out.reserve(len * 6);

  if (!filled) {
    for (int i = 0; i < len; i++) {
      int v = (i < (int)idx) ? arr[i] : 0;
      out += String(v);
      if (i != len - 1) out += ",";
    }
  } else {
    for (int i = 0; i < len; i++) {
      int p = (idx + i) % len;
      out += String(arr[p]);
      if (i != len - 1) out += ",";
    }
  }
  out += "]";
  return out;
}

static String buildHistoryArray_temp(const int16_t* arr, uint16_t idx, bool filled, int len) {
  // tempC_x10 -> float C in JSON (or null if INT16_MIN)
  String out = "[";
  out.reserve(len * 6);

  auto emit = [&](int16_t v) {
    if (v == INT16_MIN) out += "null";
    else out += String((float)v / 10.0f, 1);
  };

  if (!filled) {
    for (int i = 0; i < len; i++) {
      int16_t v = (i < (int)idx) ? arr[i] : INT16_MIN;
      emit(v);
      if (i != len - 1) out += ",";
    }
  } else {
    for (int i = 0; i < len; i++) {
      int p = (idx + i) % len;
      emit(arr[p]);
      if (i != len - 1) out += ",";
    }
  }

  out += "]";
  return out;
}

static String buildHistoryArray_u8(const uint8_t* arr, uint16_t idx, bool filled, int len) {
  String out = "[";
  out.reserve(len * 4);

  if (!filled) {
    for (int i = 0; i < len; i++) {
      uint8_t v = (i < (int)idx) ? arr[i] : 0;
      out += String(v);
      if (i != len - 1) out += ",";
    }
  } else {
    for (int i = 0; i < len; i++) {
      int p = (idx + i) % len;
      out += String(arr[p]);
      if (i != len - 1) out += ",";
    }
  }
  out += "]";
  return out;
}

// ------- HTML page (no external CDN) -------
static const char INDEX_HTML[] PROGMEM = R"====ESP_WEB_PAGE(
<!doctype html>
<html>
<head>
  <meta charset="utf-8"/>
  <meta name="viewport" content="width=device-width,initial-scale=1"/>
  <title>ESP32 Irrigation</title>
  <style>
    body{font-family:system-ui,Segoe UI,Roboto,Arial; margin:16px; max-width:1100px;}
    .row{display:flex; gap:16px; flex-wrap:wrap;}
    .card{border:1px solid #ddd; border-radius:12px; padding:12px; flex:1; min-width:320px;}
    label{display:block; margin-top:10px; font-size:14px;}
    input,select{width:100%; padding:8px; font-size:16px; border-radius:10px; border:1px solid #ccc;}
    button{margin-top:12px; padding:10px 12px; font-size:16px; border-radius:12px; border:1px solid #ccc; background:#f7f7f7; cursor:pointer;}
    .danger{border-color:#d33;}
    .kv{display:flex; justify-content:space-between; gap:10px; margin:6px 0;}
    .muted{color:#666;}
    .pill{display:inline-block; padding:4px 10px; border-radius:999px; border:1px solid #ddd; font-size:14px;}
    canvas{width:100%; height:140px; border:1px solid #eee; border-radius:12px;}
    h2,h3{margin:8px 0;}
    .two{display:grid; grid-template-columns:1fr 1fr; gap:12px;}
    @media (max-width: 860px){ .two{grid-template-columns:1fr;} }
  </style>
</head>
<body>
  <h2>ESP32 Irrigation Dashboard</h2>

  <div class="row">
    <div class="card">
      <h3>Live</h3>
      <div class="kv"><div>Moisture (ADC)</div><div class="pill" id="soil">—</div></div>
      <div class="kv"><div>Chip temp (°C)</div><div class="pill" id="temp">—</div></div>
      <div class="kv"><div>CPU usage (%)</div><div class="pill" id="cpu">—</div></div>

      <div class="kv"><div>Pump</div><div class="pill" id="pump">—</div></div>
      <div class="kv"><div>Mode</div><div class="pill" id="mode">—</div></div>
      <div class="kv"><div>Rate limit</div><div class="pill" id="lock">—</div></div>
      <div class="kv muted"><div>Window usage</div><div id="usage">—</div></div>

      <div class="two">
        <div>
          <div class="muted">Soil history</div>
          <canvas id="gSoil"></canvas>
        </div>
        <div>
          <div class="muted">Temp history</div>
          <canvas id="gTemp"></canvas>
        </div>
      </div>
      <div style="margin-top:12px;">
        <div class="muted">CPU history</div>
        <canvas id="gCpu"></canvas>
      </div>
    </div>

    <div class="card">
      <h3>Settings</h3>

      <label>Pump mode</label>
      <select id="pumpMode">
        <option value="0">OFF (never)</option>
        <option value="1">AUTO</option>
        <option value="2">ON (forced)</option>
      </select>

      <label>DRY_ON (>= : AUTO pump ON)</label>
      <input id="dryOn" type="number"/>

      <label>WET_OFF (<= : AUTO pump OFF)</label>
      <input id="wetOff" type="number"/>

      <label>PUMP PWM (0-255)</label>
      <input id="pwm" type="number"/>

      <label>Min ON (ms)</label>
      <input id="minOn" type="number"/>

      <label>Min OFF (ms)</label>
      <input id="minOff" type="number"/>

      <label>Limit window (sec)</label>
      <input id="winSec" type="number"/>

      <label>Max ON in window (sec)</label>
      <input id="maxOnSec" type="number"/>

      <label>Soil log period to SD (ms)</label>
      <input id="logMs" type="number"/>

      <label>Soft ramp (0/1)</label>
      <input id="softRamp" type="number"/>

      <button id="saveBtn">Save config</button>
      <button id="reloadBtn">Reload config</button>

      <button id="restartBtn" class="danger">RESTART device</button>

      <div class="muted" id="msg" style="margin-top:10px;"></div>
      <div class="muted">Update interval: 1s</div>
    </div>
  </div>

<script>
const $ = (id)=>document.getElementById(id);
function msg(t){ $("msg").textContent = t; }

function drawLine(canvasId, arr){
  const c = $(canvasId);
  const ctx = c.getContext("2d");
  const w = c.width  = c.clientWidth;
  const h = c.height = c.clientHeight;

  ctx.clearRect(0,0,w,h);
  ctx.beginPath();

  // min/max (ignore nulls)
  let mn = 1e9, mx = -1e9;
  for(const v of arr){
    if(v === null) continue;
    if(v < mn) mn = v;
    if(v > mx) mx = v;
  }
  if(mn === 1e9){ mn = 0; mx = 1; }
  if(mx === mn){ mx = mn + 1; }

  const n = arr.length;
  for(let i=0;i<n;i++){
    const v = arr[i];
    const vv = (v === null) ? mn : v;
    const x = (i/(n-1))*w;
    const t = (vv-mn)/(mx-mn);
    const y = h - t*h;
    if(i===0) ctx.moveTo(x,y);
    else ctx.lineTo(x,y);
  }
  ctx.stroke();
  ctx.fillText(`min:${mn} max:${mx}`, 8, 14);
}

async function fetchStatus(){
  const r = await fetch("/api/status");
  const j = await r.json();

  $("soil").textContent = j.soil;
  $("temp").textContent = (j.temp_c === null) ? "—" : j.temp_c.toFixed(1);
  $("cpu").textContent = j.cpu_pct.toFixed(1);

  $("pump").textContent = j.pump_on ? "ON" : "OFF";
  $("mode").textContent = ["OFF","AUTO","ON"][j.mode] || "—";
  $("lock").textContent = j.lockout ? "LOCKOUT" : "OK";
  $("usage").textContent = `${(j.on_time_window_ms/1000).toFixed(1)}s / ${j.max_on_sec_window}s in ${j.window_sec}s`;

  drawLine("gSoil", j.hist_soil);
  drawLine("gTemp", j.hist_temp);
  drawLine("gCpu",  j.hist_cpu);
}

async function reloadCfg(){
  const r = await fetch("/api/config");
  const j = await r.json();

  $("pumpMode").value = j.mode;
  $("dryOn").value = j.dryOn;
  $("wetOff").value = j.wetOff;
  $("pwm").value = j.pumpPwm;
  $("minOn").value = j.minOnMs;
  $("minOff").value = j.minOffMs;
  $("winSec").value = j.limitWindowSec;
  $("maxOnSec").value = j.maxOnSecInWindow;
  $("logMs").value = j.soilLogPeriodMs;
  $("softRamp").value = j.softRamp ? 1 : 0;

  msg("Config loaded.");
}

async function saveCfg(){
  const q = new URLSearchParams({
    mode: $("pumpMode").value,
    dryOn: $("dryOn").value,
    wetOff: $("wetOff").value,
    pumpPwm: $("pwm").value,
    minOnMs: $("minOn").value,
    minOffMs: $("minOff").value,
    limitWindowSec: $("winSec").value,
    maxOnSecInWindow: $("maxOnSec").value,
    soilLogPeriodMs: $("logMs").value,
    softRamp: $("softRamp").value
  });

  const r = await fetch("/api/config?" + q.toString(), { method:"POST" });
  const t = await r.text();
  msg(t);
  await reloadCfg();
}

async function restartDevice(){
  msg("Restarting...");
  await fetch("/api/restart", { method:"POST" });
  // device will reboot; page will stop responding for a bit
}

$("saveBtn").addEventListener("click", saveCfg);
$("reloadBtn").addEventListener("click", reloadCfg);
$("restartBtn").addEventListener("click", restartDevice);

reloadCfg();
fetchStatus();
setInterval(fetchStatus, 1000);
</script>
</body>
</html>
)====ESP_WEB_PAGE";

// ------- API Handlers -------
static void handleIndex() {
  webServer.send(200, "text/html", INDEX_HTML);
}

static void handleGetConfig() {
  String json = "{";
  json += "\"mode\":" + String((int)gCfg->mode) + ",";
  json += "\"dryOn\":" + String(gCfg->dryOn) + ",";
  json += "\"wetOff\":" + String(gCfg->wetOff) + ",";
  json += "\"pumpPwm\":" + String(gCfg->pumpPwm) + ",";
  json += "\"minOnMs\":" + String(gCfg->minOnMs) + ",";
  json += "\"minOffMs\":" + String(gCfg->minOffMs) + ",";
  json += "\"limitWindowSec\":" + String(gCfg->limitWindowSec) + ",";
  json += "\"maxOnSecInWindow\":" + String(gCfg->maxOnSecInWindow) + ",";
  json += "\"soilLogPeriodMs\":" + String(gCfg->soilLogPeriodMs) + ",";
  json += "\"softRamp\":" + String(gCfg->softRamp ? "true" : "false");
  json += "}";
  webServer.send(200, "application/json", json);
}

static bool hasArg(const char* k) { return webServer.hasArg(k) && webServer.arg(k).length() > 0; }

static void handleSetConfig() {
  // Save old snapshot for readable event log
  Config old = *gCfg;

  if (hasArg("mode")) gCfg->mode = (PumpMode)webServer.arg("mode").toInt();
  if (hasArg("dryOn")) gCfg->dryOn = webServer.arg("dryOn").toInt();
  if (hasArg("wetOff")) gCfg->wetOff = webServer.arg("wetOff").toInt();
  if (hasArg("pumpPwm")) gCfg->pumpPwm = webServer.arg("pumpPwm").toInt();
  if (hasArg("minOnMs")) gCfg->minOnMs = (unsigned long)webServer.arg("minOnMs").toInt();
  if (hasArg("minOffMs")) gCfg->minOffMs = (unsigned long)webServer.arg("minOffMs").toInt();
  if (hasArg("limitWindowSec")) gCfg->limitWindowSec = (unsigned long)webServer.arg("limitWindowSec").toInt();
  if (hasArg("maxOnSecInWindow")) gCfg->maxOnSecInWindow = (unsigned long)webServer.arg("maxOnSecInWindow").toInt();
  if (hasArg("soilLogPeriodMs")) gCfg->soilLogPeriodMs = (unsigned long)webServer.arg("soilLogPeriodMs").toInt();
  if (hasArg("softRamp")) gCfg->softRamp = (webServer.arg("softRamp").toInt() != 0);

  // basic sanity (same rules as main.ino)
  if (gCfg->pumpPwm < 0) gCfg->pumpPwm = 0;
  if (gCfg->pumpPwm > 255) gCfg->pumpPwm = 255;
  if (gCfg->wetOff >= gCfg->dryOn) gCfg->wetOff = gCfg->dryOn - 50;
  if (gCfg->limitWindowSec < 5) gCfg->limitWindowSec = 5;
  if (gCfg->soilLogPeriodMs < 5000) gCfg->soilLogPeriodMs = 5000;
  if (gCfg->mode > PUMP_ON) gCfg->mode = PUMP_AUTO;

  // persist
  if (storage_isReady()) {
    storage_saveConfig(*gCfg);

    // readable config change event
    String detail;
    detail.reserve(180);
    detail += "mode ";
    detail += String((int)old.mode) + "->" + String((int)gCfg->mode);
    detail += " dryOn " + String(old.dryOn) + "->" + String(gCfg->dryOn);
    detail += " wetOff " + String(old.wetOff) + "->" + String(gCfg->wetOff);
    detail += " pwm " + String(old.pumpPwm) + "->" + String(gCfg->pumpPwm);
    detail += " win " + String(old.limitWindowSec) + "->" + String(gCfg->limitWindowSec);
    detail += " maxOn " + String(old.maxOnSecInWindow) + "->" + String(gCfg->maxOnSecInWindow);
    detail += " logMs " + String(old.soilLogPeriodMs) + "->" + String(gCfg->soilLogPeriodMs);
    storage_logEvent("CONFIG", detail);
  }

  webServer.send(200, "text/plain", "OK: config saved");
}

static void handleStatus() {
  // histories
  String hs = buildHistoryArray_int(gHist->soil, gHist->idx, gHist->filled, HIST_LEN);
  String ht = buildHistoryArray_temp(gHist->tempC_x10, gHist->idx, gHist->filled, HIST_LEN);
  String hc = buildHistoryArray_u8(gHist->cpuPct, gHist->idx, gHist->filled, HIST_LEN);

  String json = "{";
  json += "\"soil\":" + String(gRt->soilNow) + ",";
  if (isnan(gRt->chipTempC)) json += "\"temp_c\":null,";
  else json += "\"temp_c\":" + String(gRt->chipTempC, 1) + ",";
  json += "\"cpu_pct\":" + String(gRt->cpuUsagePct, 1) + ",";
  json += "\"pump_on\":" + jsonBool(gRt->pumpOn) + ",";
  json += "\"lockout\":" + jsonBool(gRt->lockout) + ",";
  json += "\"mode\":" + String((int)gCfg->mode) + ",";
  json += "\"window_sec\":" + String(gCfg->limitWindowSec) + ",";
  json += "\"max_on_sec_window\":" + String(gCfg->maxOnSecInWindow) + ",";
  json += "\"on_time_window_ms\":" + String(gRt->onTimeThisWindowMs) + ",";
  json += "\"hist_soil\":" + hs + ",";
  json += "\"hist_temp\":" + ht + ",";
  json += "\"hist_cpu\":" + hc;
  json += "}";
  webServer.send(200, "application/json", json);
}

static void handleRestart() {
  webServer.send(200, "text/plain", "OK: restarting");
  if (storage_isReady()) storage_logEvent("SYSTEM", "restart_via_web");
  delay(200);
  ESP.restart();
}

// ------- Public API -------
static void web_begin(Config* cfg, Runtime* rt, Histories* hist) {
  gCfg = cfg;
  gRt = rt;
  gHist = hist;

  webServer.on("/", handleIndex);
  webServer.on("/api/status", HTTP_GET, handleStatus);
  webServer.on("/api/config", HTTP_GET, handleGetConfig);
  webServer.on("/api/config", HTTP_POST, handleSetConfig);
  webServer.on("/api/restart", HTTP_POST, handleRestart);

  webServer.begin();
  Serial.println("[WEB] server started");
}

static void web_handleClient() {
  webServer.handleClient();
}
