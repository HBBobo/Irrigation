// ESP32 Irrigation Web UI - App.js

const $ = id => document.getElementById(id);
const canvas = $("chart");
const ctx = canvas.getContext("2d");

// Auto-refresh intervals (ms)
const STATUS_INTERVAL = 3000;
const HISTORY_INTERVAL = 15000;

// History data storage
let historyData = { soil: [], temp: [], cpu: [], idx: 0, len: 0 };
let logPeriodMs = 5000; // Default, will be updated from config

// Prevent concurrent requests
let fetchInProgress = false;

// Fetch JSON helper with timeout
async function fetchJSON(url, timeoutMs = 5000) {
  if (fetchInProgress) return null;
  fetchInProgress = true;

  try {
    const controller = new AbortController();
    const timeout = setTimeout(() => controller.abort(), timeoutMs);

    const res = await fetch(url, { signal: controller.signal });
    clearTimeout(timeout);

    if (!res.ok) throw new Error("HTTP " + res.status);
    return res.json();
  } finally {
    fetchInProgress = false;
  }
}

// Update timestamp
function updateTime() {
  $("lastUpdate").textContent = new Date().toLocaleTimeString();
}

// Load real-time status
async function loadStatus() {
  try {
    const s = await fetchJSON("/api/status");
    if (!s) return; // Request skipped (another in progress)

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
    if (!c) return;

    $("dryOn").value = c.dryOn;
    $("wetOff").value = c.wetOff;
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
    const res = await fetch("/api/config/set", {
      method: "POST",
      headers: { "Content-Type": "application/x-www-form-urlencoded" },
      body: params.toString()
    });
    if (!res.ok) throw new Error("HTTP " + res.status);
    loadStatus();
  } catch (e) {
    alert("Failed to apply mode: " + e.message);
  }
}

// Save all config
async function saveConfig() {
  const params = new URLSearchParams();
  params.append("dryOn", $("dryOn").value);
  params.append("wetOff", $("wetOff").value);
  params.append("mode", $("mode").value);

  try {
    const res = await fetch("/api/config/set", {
      method: "POST",
      headers: { "Content-Type": "application/x-www-form-urlencoded" },
      body: params.toString()
    });
    if (!res.ok) throw new Error("HTTP " + res.status);
    alert("Config saved!");
    loadConfig();
  } catch (e) {
    alert("Failed to save config: " + e.message);
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
    const h = await fetchJSON("/api/history", 10000); // Longer timeout for history
    if (!h) return; // Request skipped

    if (h.len > 0) {
      // Reorder circular buffer to linear array (oldest first)
      const reorder = (arr, idx, len, filled) => {
        if (!arr || arr.length === 0) return [];
        if (!filled) return arr.slice(0, len);
        // Circular buffer: data from idx to end, then 0 to idx
        return [...arr.slice(idx), ...arr.slice(0, idx)];
      };

      const filled = h.len >= 240; // HIST_LEN from config.h
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

// ---- File Browser ----
let currentPath = "/";

function fsFormatSize(bytes) {
  if (bytes < 1024) return bytes + " B";
  if (bytes < 1024 * 1024) return (bytes / 1024).toFixed(1) + " KB";
  return (bytes / (1024 * 1024)).toFixed(1) + " MB";
}

// Separate fetch for file browser (doesn't use global lock)
async function fsFetch(url, options = {}) {
  const controller = new AbortController();
  const timeout = setTimeout(() => controller.abort(), 10000);
  try {
    const res = await fetch(url, { ...options, signal: controller.signal });
    clearTimeout(timeout);
    return res;
  } catch (e) {
    clearTimeout(timeout);
    throw e;
  }
}

async function fsLoadDir(path) {
  currentPath = path || "/";
  $("fsPath").textContent = currentPath;

  try {
    const res = await fsFetch("/api/fs/list?path=" + encodeURIComponent(currentPath));
    if (!res.ok) throw new Error("HTTP " + res.status);
    const data = await res.json();
    const list = $("fsList");
    list.innerHTML = "";

    if (!data.items || data.items.length === 0) {
      list.innerHTML = "<div class='file-item'>Empty directory</div>";
      return;
    }

    data.items.forEach(item => {
      const div = document.createElement("div");
      div.className = "file-item";

      const name = document.createElement("span");
      name.className = "file-name";
      name.textContent = (item.dir ? "📁 " : "📄 ") + item.name;
      if (item.dir) {
        name.style.cursor = "pointer";
        name.onclick = () => fsLoadDir(currentPath + (currentPath === "/" ? "" : "/") + item.name);
      }

      const size = document.createElement("span");
      size.className = "file-size";
      size.textContent = item.dir ? "" : fsFormatSize(item.size);

      const actions = document.createElement("span");
      actions.className = "file-actions";

      if (!item.dir) {
        const dl = document.createElement("button");
        dl.textContent = "↓";
        dl.title = "Download";
        dl.onclick = () => fsDownload(currentPath + (currentPath === "/" ? "" : "/") + item.name, item.name);
        actions.appendChild(dl);
      }

      const del = document.createElement("button");
      del.textContent = "×";
      del.title = "Delete";
      del.className = "warn";
      del.onclick = () => fsDelete(currentPath + (currentPath === "/" ? "" : "/") + item.name);
      actions.appendChild(del);

      div.appendChild(name);
      div.appendChild(size);
      div.appendChild(actions);
      list.appendChild(div);
    });
  } catch (e) {
    console.error("FS list error:", e);
    $("fsList").innerHTML = "<div class='file-item'>Error loading directory</div>";
  }
}

function fsUp() {
  if (currentPath === "/") return;
  const parts = currentPath.split("/").filter(p => p);
  parts.pop();
  fsLoadDir("/" + parts.join("/"));
}

function fsRefresh() {
  fsLoadDir(currentPath);
}

async function fsDownload(path, filename) {
  try {
    const res = await fetch("/api/fs/download?path=" + encodeURIComponent(path));
    if (!res.ok) throw new Error("Download failed");

    const blob = await res.blob();
    const url = URL.createObjectURL(blob);
    const a = document.createElement("a");
    a.href = url;
    a.download = filename;
    a.click();
    URL.revokeObjectURL(url);
  } catch (e) {
    alert("Download failed: " + e.message);
  }
}

async function fsDelete(path) {
  if (!confirm("Delete " + path + "?")) return;

  try {
    const res = await fetch("/api/fs/delete?path=" + encodeURIComponent(path), { method: "POST" });
    const data = await res.json();
    if (data.ok) {
      fsRefresh();
    } else {
      alert("Delete failed: " + (data.error || "unknown error"));
    }
  } catch (e) {
    alert("Delete failed: " + e.message);
  }
}

async function fsUpload() {
  const input = $("fsUploadFile");
  if (!input.files || !input.files[0]) {
    alert("Select a file first");
    return;
  }

  const file = input.files[0];
  const path = currentPath + (currentPath === "/" ? "" : "/") + file.name;

  try {
    // Read file and upload in chunks
    const CHUNK_SIZE = 4096;
    const reader = new FileReader();

    reader.onload = async function(e) {
      const data = new Uint8Array(e.target.result);
      let offset = 0;

      while (offset < data.length) {
        const chunk = data.slice(offset, offset + CHUNK_SIZE);
        const append = offset > 0 ? "1" : "0";

        const res = await fetch("/api/fs/upload?path=" + encodeURIComponent(path) + "&append=" + append, {
          method: "POST",
          headers: { "Content-Type": "text/plain" },
          body: String.fromCharCode.apply(null, chunk)
        });

        if (!res.ok) throw new Error("Upload failed at offset " + offset);
        offset += chunk.length;
      }

      alert("Upload complete!");
      input.value = "";
      fsRefresh();
    };

    reader.readAsArrayBuffer(file);
  } catch (e) {
    alert("Upload failed: " + e.message);
  }
}

// Initialize
document.addEventListener("DOMContentLoaded", () => {
  loadAll();
  fsLoadDir("/");

  // Auto-refresh status (every 2 seconds)
  setInterval(loadStatus, STATUS_INTERVAL);

  // Auto-refresh history (every 10 seconds)
  setInterval(loadHistory, HISTORY_INTERVAL);
});

// Handle window resize for chart
window.addEventListener("resize", drawChart);
