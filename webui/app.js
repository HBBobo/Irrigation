// ESP32 Irrigation Web UI - App.js

const $ = id => document.getElementById(id);
const canvas = $("chart");
const ctx = canvas.getContext("2d");

// Auto-refresh intervals (ms)
const STATUS_INTERVAL = 2000;
const HISTORY_INTERVAL = 10000;

// History data storage
let historyData = { soil: [], temp: [], cpu: [], idx: 0, len: 0 };
let logPeriodMs = 5000; // Default, will be updated from config

// Fetch JSON helper
async function fetchJSON(url) {
  const res = await fetch(url);
  if (!res.ok) throw new Error("HTTP " + res.status);
  return res.json();
}

// Update timestamp
function updateTime() {
  $("lastUpdate").textContent = new Date().toLocaleTimeString();
}

// Load real-time status
async function loadStatus() {
  try {
    const s = await fetchJSON("/api/status");

    $("soil").textContent = s.soil;
    $("temp").textContent = s.tempC.toFixed(1);
    $("cpu").textContent = s.cpuPct + "%";

    const pumpEl = $("pump");
    pumpEl.textContent = s.pumpOn ? "ON" : "OFF";
    pumpEl.className = "stat " + (s.pumpOn ? "on" : "off");

    $("lockout").textContent = s.lockout ? "YES" : "No";
    $("onTime").textContent = s.onTime || 0;

    // Sync mode dropdown if changed externally
    if ($("mode").value != s.mode) {
      $("mode").value = s.mode;
    }

    updateTime();
  } catch (e) {
    console.error("Status error:", e);
  }
}

// Load config values
async function loadConfig() {
  try {
    const c = await fetchJSON("/api/config/get");

    $("dryOn").value = c.dryOn;
    $("wetOff").value = c.wetOff;
    $("pumpPwm").value = c.pumpPwm;
    $("mode").value = c.mode;

    // Store log period for chart calculations
    if (c.logPeriodMs) {
      logPeriodMs = c.logPeriodMs;
    }
  } catch (e) {
    console.error("Config error:", e);
  }
}

// Apply mode only
async function applyMode() {
  const mode = $("mode").value;

  const params = new URLSearchParams();
  params.append("mode", mode);

  try {
    await fetch("/api/config/set", {
      method: "POST",
      body: params
    });
    loadStatus();
  } catch (e) {
    alert("Failed to apply mode");
  }
}

// Save all config
async function saveConfig() {
  const params = new URLSearchParams();
  params.append("dryOn", $("dryOn").value);
  params.append("wetOff", $("wetOff").value);
  params.append("pumpPwm", $("pumpPwm").value);
  params.append("mode", $("mode").value);

  try {
    await fetch("/api/config/set", {
      method: "POST",
      body: params
    });
    alert("Config saved!");
    loadConfig();
  } catch (e) {
    alert("Failed to save config");
  }
}

// Restart ESP
async function restart() {
  if (!confirm("Restart ESP32?")) return;

  try {
    await fetch("/api/restart", { method: "POST" });
    alert("Restarting... Page will reload in 5 seconds.");
    setTimeout(() => location.reload(), 5000);
  } catch (e) {
    alert("Failed to restart");
  }
}

// Update WebUI from GitHub
async function updateWebUI() {
  if (!confirm("Download latest WebUI from GitHub and restart?")) return;

  try {
    await fetch("/api/webui/update", { method: "POST" });
    alert("Updating WebUI... Page will reload in 10 seconds.");
    setTimeout(() => location.reload(), 10000);
  } catch (e) {
    alert("Failed to update WebUI");
  }
}

// Get chart color based on metric
function getChartColor(metric) {
  switch (metric) {
    case "soil": return "#4caf50";
    case "temp": return "#ff9800";
    case "cpu": return "#2196f3";
    default: return "#4caf50";
  }
}

// Get chart label based on metric
function getChartLabel(metric) {
  switch (metric) {
    case "soil": return "Soil ADC";
    case "temp": return "Temp C";
    case "cpu": return "CPU %";
    default: return "";
  }
}

// Draw chart with selected metric and time window
function drawChart() {
  const metric = $("chartMetric").value;
  const windowSec = parseInt($("chartWindow").value);

  let arr = historyData[metric] || [];
  if (!arr || arr.length < 2) {
    // Clear canvas if no data
    const rect = canvas.getBoundingClientRect();
    canvas.width = rect.width * window.devicePixelRatio;
    canvas.height = rect.height * window.devicePixelRatio;
    ctx.scale(window.devicePixelRatio, window.devicePixelRatio);
    ctx.fillStyle = "#000";
    ctx.fillRect(0, 0, rect.width, rect.height);
    ctx.fillStyle = "#666";
    ctx.font = "14px sans-serif";
    ctx.textAlign = "center";
    ctx.fillText("No data yet", rect.width / 2, rect.height / 2);
    return;
  }

  // Calculate how many points to show based on time window
  let pointsToShow = arr.length;
  if (windowSec > 0) {
    const pointsPerSec = 1000 / logPeriodMs;
    pointsToShow = Math.min(arr.length, Math.ceil(windowSec * pointsPerSec));
  }

  // Get the last N points
  const displayArr = arr.slice(-pointsToShow);

  const color = getChartColor(metric);
  const label = getChartLabel(metric);

  // Set canvas size
  const rect = canvas.getBoundingClientRect();
  canvas.width = rect.width * window.devicePixelRatio;
  canvas.height = rect.height * window.devicePixelRatio;
  ctx.scale(window.devicePixelRatio, window.devicePixelRatio);

  const w = rect.width;
  const h = rect.height;

  // Clear
  ctx.fillStyle = "#000";
  ctx.fillRect(0, 0, w, h);

  if (displayArr.length < 2) return;

  // Calculate range
  const min = Math.min(...displayArr);
  const max = Math.max(...displayArr);
  const span = max - min || 1;
  const padding = 40;

  // Draw grid lines
  ctx.strokeStyle = "#333";
  ctx.lineWidth = 1;
  for (let i = 0; i <= 4; i++) {
    const y = padding + (h - 2 * padding) * i / 4;
    ctx.beginPath();
    ctx.moveTo(padding, y);
    ctx.lineTo(w - 10, y);
    ctx.stroke();

    // Label
    const val = max - (span * i / 4);
    ctx.fillStyle = "#666";
    ctx.font = "10px sans-serif";
    ctx.textAlign = "right";
    ctx.fillText(val.toFixed(1), padding - 5, y + 3);
  }

  // Draw data line
  ctx.strokeStyle = color;
  ctx.lineWidth = 2;
  ctx.beginPath();

  displayArr.forEach((v, i) => {
    const x = padding + (i / (displayArr.length - 1)) * (w - padding - 10);
    const y = padding + ((max - v) / span) * (h - 2 * padding);

    if (i === 0) ctx.moveTo(x, y);
    else ctx.lineTo(x, y);
  });

  ctx.stroke();

  // Draw current value and label
  ctx.fillStyle = color;
  ctx.font = "bold 14px sans-serif";
  ctx.textAlign = "left";
  ctx.fillText(label + ": " + displayArr[displayArr.length - 1].toFixed(1), padding + 5, 20);

  // Draw point count
  ctx.fillStyle = "#666";
  ctx.font = "10px sans-serif";
  ctx.textAlign = "right";
  ctx.fillText(displayArr.length + " points", w - 10, 15);
}

// Load history from ESP
async function loadHistory() {
  try {
    const h = await fetchJSON("/api/history");

    if (h && h.len > 0) {
      // Reorder circular buffer to linear array (oldest first)
      const reorder = (arr, idx, len, filled) => {
        if (!arr || arr.length === 0) return [];
        if (!filled) return arr.slice(0, len);
        // Circular buffer: data from idx to end, then 0 to idx
        return [...arr.slice(idx), ...arr.slice(0, idx)];
      };

      const filled = h.len >= 120; // HIST_LEN from config.h
      historyData.soil = reorder(h.soil, h.idx, h.len, filled);
      historyData.temp = reorder(h.temp, h.idx, h.len, filled);
      historyData.cpu = reorder(h.cpu, h.idx, h.len, filled);
      historyData.len = h.len;
      historyData.idx = h.idx;

      drawChart();
    }
  } catch (e) {
    console.error("History error:", e);
  }
}

// Load all data
function loadAll() {
  loadStatus();
  loadConfig();
  loadHistory();
}

// Initialize
document.addEventListener("DOMContentLoaded", () => {
  loadAll();

  // Auto-refresh status (every 2 seconds)
  setInterval(loadStatus, STATUS_INTERVAL);

  // Auto-refresh history (every 10 seconds)
  setInterval(loadHistory, HISTORY_INTERVAL);
});

// Handle window resize for chart
window.addEventListener("resize", drawChart);
