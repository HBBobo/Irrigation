// ESP32 Irrigation Web UI - App.js

const $ = id => document.getElementById(id);
const canvas = $("chart");
const ctx = canvas.getContext("2d");

// Auto-refresh interval (ms)
const STATUS_INTERVAL = 2000;
const HISTORY_INTERVAL = 30000;

// Fetch JSON helper
async function fetchJSON(url) {
  const res = await fetch(url);
  if (!res.ok) throw new Error(`HTTP ${res.status}`);
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
    alert("Restarting... Page will reload in 10 seconds.");
    setTimeout(() => location.reload(), 10000);
  } catch (e) {
    alert("Failed to restart");
  }
}

// Update WebUI from GitHub
async function updateWebUI() {
  if (!confirm("Download latest WebUI from GitHub and restart?")) return;

  try {
    await fetch("/api/webui/update", { method: "POST" });
    alert("Updating WebUI... Page will reload in 15 seconds.");
    setTimeout(() => location.reload(), 15000);
  } catch (e) {
    alert("Failed to update WebUI");
  }
}

// Draw chart
function drawChart(arr, color = "#4caf50") {
  if (!arr || arr.length < 2) return;

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

  // Calculate range
  const min = Math.min(...arr);
  const max = Math.max(...arr);
  const span = max - min || 1;
  const padding = 30;

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
    ctx.fillText(val.toFixed(0), padding - 5, y + 3);
  }

  // Draw data line
  ctx.strokeStyle = color;
  ctx.lineWidth = 2;
  ctx.beginPath();

  arr.forEach((v, i) => {
    const x = padding + (i / (arr.length - 1)) * (w - padding - 10);
    const y = padding + ((max - v) / span) * (h - 2 * padding);

    if (i === 0) ctx.moveTo(x, y);
    else ctx.lineTo(x, y);
  });

  ctx.stroke();

  // Draw current value
  ctx.fillStyle = color;
  ctx.font = "bold 14px sans-serif";
  ctx.textAlign = "left";
  ctx.fillText("Current: " + arr[arr.length - 1], padding + 5, 20);
}

// Load history
async function loadHistory() {
  try {
    const h = await fetchJSON("/api/history");
    if (h.soil && h.soil.length > 0) {
      drawChart(h.soil);
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

  // Auto-refresh status
  setInterval(loadStatus, STATUS_INTERVAL);

  // Auto-refresh history (less frequent)
  setInterval(loadHistory, HISTORY_INTERVAL);
});

// Handle window resize for chart
window.addEventListener("resize", loadHistory);
