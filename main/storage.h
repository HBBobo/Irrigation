#pragma once
#include <Arduino.h>
#include <SdFat.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <esp_task_wdt.h>

#include "config.h"

extern const char* FW_VERSION;

static SdFat sd;
static bool g_sdReady = false;

// ---- paths
static const char* PATH_CFG  = "/cfg.txt";
static const char* PATH_HIST = "/hist.bin";
static const char* PATH_LOG  = "/log.csv";

// Web UI on SD
static const char* WEB_DIR = "/web";
static const char* WEB_INDEX = "/web/index.html";

// GitHub raw base (UI files)
static const char* GH_WEB_BASE =
  "https://raw.githubusercontent.com/HBBobo/Irrigation/main/webui";

// GitHub/DigiCert Root CA certificate for secure downloads
// This is DigiCert Global Root CA - valid for raw.githubusercontent.com
static const char* GITHUB_ROOT_CA = R"EOF(
-----BEGIN CERTIFICATE-----
MIIDrzCCApegAwIBAgIQCDvgVpBCRrGhdWrJWZHHSjANBgkqhkiG9w0BAQUFADBh
MQswCQYDVQQGEwJVUzEVMBMGA1UEChMMRGlnaUNlcnQgSW5jMRkwFwYDVQQLExB3
d3cuZGlnaWNlcnQuY29tMSAwHgYDVQQDExdEaWdpQ2VydCBHbG9iYWwgUm9vdCBD
QTAeFw0wNjExMTAwMDAwMDBaFw0zMTExMTAwMDAwMDBaMGExCzAJBgNVBAYTAlVT
MRUwEwYDVQQKEwxEaWdpQ2VydCBJbmMxGTAXBgNVBAsTEHd3dy5kaWdpY2VydC5j
b20xIDAeBgNVBAMTF0RpZ2lDZXJ0IEdsb2JhbCBSb290IENBMIIBIjANBgkqhkiG
9w0BAQEFAAOCAQ8AMIIBCgKCAQEA4jvhEXLeqKTTo1eqUKKPC3eQyaKl7hLOllsB
CSDMAZOnTjC3U/dDxGkAV53ijSLdhwZAAIEJzs4bg7/fzTtxRuLWZscFs3YnFo97
nh6Vfe63SKMI2tavegw5BmV/Sl0fvBf4q77uKNd0f3p4mVmFaG5cIzJLv07A6Fpt
43C/dxC//AH2hdmoRBBYMql1GNXRor5H4idq9Joz+EkIYIvUX7Q6hL+hqkpMfT7P
T19sdl6gSzeRntwi5m3OFBqOasv+zbMUZBfHWymeMr/y7vrTC0LUq7dBMtoM1O/4
gdW7jVg/tRvoSSiicNoxBN33shbyTApOB6jtSj1etX+jkMOvJwIDAQABo2MwYTAO
BgNVHQ8BAf8EBAMCAYYwDwYDVR0TAQH/BAUwAwEB/zAdBgNVHQ4EFgQUA95QNVbR
TLtm8KPiGxvDl7I90VUwHwYDVR0jBBgwFoAUA95QNVbRTLtm8KPiGxvDl7I90VUw
DQYJKoZIhvcNAQEFBQADggEBAMucN6pIExIK+t1EnE9SsPTfrgT1eXkIoyQY/Esr
hMAtudXH/vTBH1jLuG2cenTnmCmrEbXjcKChzUyImZOMkXDiqw8cvpOp/2PV5Adg
06O/nVsJ8dWO41P0jmP6P6fbtGbfYmbW0W5BjfIttep3Sp+dWOIrWcBAI+0tKIJF
PnlUkiaY4IBIqDfv8NZ5YBberOgOzW6sRBc4L0na4UU+Krk2U886UAb3LujEV0ls
YSEY1QSteDwsOoBrp+uvFRTp2InBuThs4pFsiv9kuXclVzDAGySj4dzp30d8tbQk
CAUw7C29C79Fv1C5qfPrmAESrciIxpg0X40KPMbp1ZWVbd4=
-----END CERTIFICATE-----
)EOF";

// ---- history blob
static const uint32_t HISTORY_MAGIC = 0xB0B0B0B0;

struct HistoryBlob {
  uint32_t magic;
  uint16_t len;
  uint16_t idx;
  uint8_t  filled;
  uint8_t  rsv[3];
  int16_t  soil[HIST_LEN];
  int16_t  tempC_x10[HIST_LEN];
  uint8_t  cpuPct[HIST_LEN];
};

static bool storage_isReady() { return g_sdReady; }

static bool storage_begin(int cs, int sck, int miso, int mosi) {
  (void)sck; (void)miso; (void)mosi;
  SdSpiConfig cfg(cs, DEDICATED_SPI, SD_SCK_MHZ(8));
  Serial.println("[SD] SdFat init...");
  g_sdReady = sd.begin(cfg);
  if (g_sdReady) Serial.println("[SD] init OK (SdFat)");
  else Serial.println("[SD] init FAIL");
  return g_sdReady;
}

static void storage_mkdirs() {
  if (!g_sdReady) return;
  if (!sd.exists(WEB_DIR)) sd.mkdir(WEB_DIR);
}

// ---- Config validation ----
static void storage_validateConfig(Config& cfg) {
  // Clamp values to valid ranges
  cfg.dryOn = constrain(cfg.dryOn, 0, 4095);
  cfg.wetOff = constrain(cfg.wetOff, 0, 4095);
  cfg.pumpPwm = constrain(cfg.pumpPwm, 0, 255);
  cfg.minOnMs = constrain(cfg.minOnMs, 1000UL, 60000UL);
  cfg.minOffMs = constrain(cfg.minOffMs, 1000UL, 60000UL);
  cfg.maxOnSecInWindow = constrain(cfg.maxOnSecInWindow, 10UL, 300UL);
  cfg.limitWindowSec = constrain(cfg.limitWindowSec, 60UL, 3600UL);
  cfg.logPeriodMs = constrain(cfg.logPeriodMs, 1000UL, 60000UL);

  // Ensure mode is valid
  if (cfg.mode > PUMP_ON) {
    cfg.mode = PUMP_AUTO;
  }

  // Ensure dryOn > wetOff for proper hysteresis
  if (cfg.dryOn <= cfg.wetOff) {
    cfg.dryOn = cfg.wetOff + 300;
    if (cfg.dryOn > 4095) {
      cfg.dryOn = 4095;
      cfg.wetOff = 3795;
    }
  }

  Serial.println("[CFG] validated");
}

static bool storage_saveConfig(const Config& cfg) {
  if (!g_sdReady) return false;
  FsFile f = sd.open(PATH_CFG, O_WRITE | O_CREAT | O_TRUNC);
  if (!f) return false;

  f.printf("dryOn=%d\n", cfg.dryOn);
  f.printf("wetOff=%d\n", cfg.wetOff);
  f.printf("pumpPwm=%d\n", cfg.pumpPwm);
  f.printf("softRamp=%d\n", cfg.softRamp ? 1 : 0);
  f.printf("minOnMs=%lu\n", (unsigned long)cfg.minOnMs);
  f.printf("minOffMs=%lu\n", (unsigned long)cfg.minOffMs);
  f.printf("limitWindowSec=%lu\n", (unsigned long)cfg.limitWindowSec);
  f.printf("maxOnSecInWindow=%lu\n", (unsigned long)cfg.maxOnSecInWindow);
  f.printf("logPeriodMs=%lu\n", (unsigned long)cfg.logPeriodMs);
  f.printf("mode=%d\n", (int)cfg.mode);

  f.close();
  return true;
}

static bool storage_loadConfig(Config& cfg) {
  if (!g_sdReady) return false;
  FsFile f = sd.open(PATH_CFG, O_RDONLY);
  if (!f) return false;

  String line;
  while (f.available()) {
    line = f.readStringUntil('\n');
    line.trim();
    if (!line.length()) continue;
    int eq = line.indexOf('=');
    if (eq < 0) continue;
    String k = line.substring(0, eq);
    String v = line.substring(eq + 1);

    long iv = v.toInt();
    if (k == "dryOn") cfg.dryOn = (int)iv;
    else if (k == "wetOff") cfg.wetOff = (int)iv;
    else if (k == "pumpPwm") cfg.pumpPwm = (int)iv;
    else if (k == "softRamp") cfg.softRamp = (iv != 0);
    else if (k == "minOnMs") cfg.minOnMs = (uint32_t)iv;
    else if (k == "minOffMs") cfg.minOffMs = (uint32_t)iv;
    else if (k == "limitWindowSec") cfg.limitWindowSec = (uint32_t)iv;
    else if (k == "maxOnSecInWindow") cfg.maxOnSecInWindow = (uint32_t)iv;
    else if (k == "logPeriodMs") cfg.logPeriodMs = (uint32_t)iv;
    else if (k == "mode") cfg.mode = (PumpMode)iv;
  }

  f.close();
  return true;
}

static bool storage_saveHistory(const Histories& h) {
  if (!g_sdReady) return false;

  HistoryBlob hb{};
  hb.magic = HISTORY_MAGIC;
  hb.len = HIST_LEN;
  hb.idx = h.idx;
  hb.filled = h.filled ? 1 : 0;
  memcpy(hb.soil, h.soil, sizeof(hb.soil));
  memcpy(hb.tempC_x10, h.tempC_x10, sizeof(hb.tempC_x10));
  memcpy(hb.cpuPct, h.cpuPct, sizeof(hb.cpuPct));

  FsFile f = sd.open(PATH_HIST, O_WRITE | O_CREAT | O_TRUNC);
  if (!f) return false;

  size_t n = f.write(&hb, sizeof(hb));
  f.close();
  return n == sizeof(hb);
}

static bool storage_loadHistory(Histories& h) {
  if (!g_sdReady) return false;

  FsFile f = sd.open(PATH_HIST, O_RDONLY);
  if (!f) return false;

  HistoryBlob hb{};
  bool success = false;

  if (f.read(&hb, sizeof(hb)) == sizeof(hb)) {
    if (hb.magic == HISTORY_MAGIC && hb.len == HIST_LEN) {
      h.idx = hb.idx;
      h.filled = hb.filled != 0;
      memcpy(h.soil, hb.soil, sizeof(h.soil));
      memcpy(h.tempC_x10, hb.tempC_x10, sizeof(h.tempC_x10));
      memcpy(h.cpuPct, hb.cpuPct, sizeof(h.cpuPct));
      success = true;
    }
  }

  f.close();  // Always close file
  return success;
}

static void storage_appendLog(const Runtime& rt) {
  if (!g_sdReady) return;

  bool exists = sd.exists(PATH_LOG);
  FsFile f = sd.open(PATH_LOG, O_WRITE | O_CREAT | O_APPEND);
  if (!f) return;

  if (!exists) {
    f.println("ms,soil,tempC_x10,cpuPct,pumpOn,lockout,onTimeWindowMs");
  }

  f.printf("%lu,%d,%d,%u,%d,%d,%lu\n",
           (unsigned long)millis(),
           rt.soilNow,
           (int)rt.tempC_x10,
           (unsigned)rt.cpuPct,
           rt.pumpOn ? 1 : 0,
           rt.lockout ? 1 : 0,
           (unsigned long)rt.onTimeThisWindowMs);

  f.close();
}

// ----- GitHub web UI cache: download file in chunks
// Downloads in 4KB chunks with delays to avoid watchdog timeout
static bool storage_downloadToFile(const String& url, const char* outPath, uint32_t timeoutMs = 30000) {
  if (!g_sdReady) return false;

  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(10);

  HTTPClient http;
  http.setTimeout(10000);

  if (!http.begin(client, url)) {
    Serial.println("[SD] HTTP begin failed");
    return false;
  }

  // Get file size first with HEAD request
  const char* hdrs[] = {"Content-Length"};
  http.collectHeaders(hdrs, 1);

  int code = http.GET();
  if (code != 200) {
    Serial.printf("[SD] HTTP GET failed: %d\n", code);
    http.end();
    return false;
  }

  size_t totalSize = http.getSize();
  http.end();

  if (totalSize <= 0) {
    Serial.println("[SD] Unknown file size");
    return false;
  }

  Serial.printf("[SD] File size: %u bytes\n", (unsigned)totalSize);

  // Download in chunks using Range requests
  const size_t CHUNK_SIZE = 4096;
  size_t downloaded = 0;

  FsFile f = sd.open(outPath, O_WRITE | O_CREAT | O_TRUNC);
  if (!f) {
    Serial.println("[SD] File open failed");
    return false;
  }

  while (downloaded < totalSize) {
    size_t chunkEnd = min(downloaded + CHUNK_SIZE - 1, totalSize - 1);

    // Create new connection for each chunk
    WiFiClientSecure chunkClient;
    chunkClient.setInsecure();
    chunkClient.setTimeout(10);

    HTTPClient chunkHttp;
    chunkHttp.setTimeout(10000);

    if (!chunkHttp.begin(chunkClient, url)) {
      Serial.println("[SD] Chunk HTTP begin failed");
      f.close();
      return false;
    }

    // Request specific byte range
    char rangeHeader[32];
    snprintf(rangeHeader, sizeof(rangeHeader), "bytes=%u-%u", (unsigned)downloaded, (unsigned)chunkEnd);
    chunkHttp.addHeader("Range", rangeHeader);

    int chunkCode = chunkHttp.GET();
    if (chunkCode != 200 && chunkCode != 206) {
      Serial.printf("[SD] Chunk GET failed: %d\n", chunkCode);
      chunkHttp.end();
      f.close();
      return false;
    }

    // Read chunk data
    WiFiClient* stream = chunkHttp.getStreamPtr();
    uint8_t buf[512];
    size_t chunkDownloaded = 0;
    size_t expectedChunk = chunkEnd - downloaded + 1;

    while (chunkDownloaded < expectedChunk && stream->connected()) {
      int avail = stream->available();
      if (avail > 0) {
        int n = stream->readBytes((char*)buf, min((size_t)avail, sizeof(buf)));
        if (n > 0) {
          f.write(buf, n);
          chunkDownloaded += n;
        }
      } else {
        delay(5);
      }
    }

    chunkHttp.end();
    downloaded += chunkDownloaded;

    // Progress and yield between chunks
    Serial.printf("[SD] %u/%u bytes\n", (unsigned)downloaded, (unsigned)totalSize);
    delay(50);  // Give system time between chunks
  }

  f.close();
  Serial.printf("[SD] Downloaded %u bytes\n", (unsigned)downloaded);
  return downloaded == totalSize;
}

// Web UI files to download from GitHub
static const char* WEB_FILES[] = {
  "index.html",
  "app.js",
  "style.css"
};
static const int WEB_FILES_COUNT = 3;

// Expected file sizes (parsed from firmware.json)
static size_t g_webFileSizes[3] = {0, 0, 0};

// firmware.json URL for version checking
static const char* FIRMWARE_JSON_URL =
  "https://raw.githubusercontent.com/HBBobo/Irrigation/main/firmware/firmware.json";

// Local webui version stored on SD
static const char* LOCAL_WEBUI_VERSION_FILE = "/web/.version";

static void storage_downloadWebFile(const char* filename, bool wifiUp) {
  if (!wifiUp) return;

  char localPath[32];
  snprintf(localPath, sizeof(localPath), "/web/%s", filename);

  String url = String(GH_WEB_BASE) + "/" + filename;
  Serial.print("[SD] downloading ");
  Serial.println(url);

  // Try up to 3 times with longer timeout for larger files
  for (int attempt = 0; attempt < 3; attempt++) {
    if (attempt > 0) {
      Serial.printf("[SD] retry %d...\n", attempt);
      delay(2000);  // Wait before retry
    }

    if (storage_downloadToFile(url, localPath, 30000)) {  // 30 second timeout
      Serial.printf("[SD] %s OK\n", filename);
      return;
    }
  }

  Serial.printf("[SD] %s FAIL after 3 attempts\n", filename);
}

// Parse file size from JSON for a specific file
// Looks for "filename": size pattern
static size_t storage_parseFileSize(const String& json, const char* filename) {
  String pattern = String("\"") + filename + "\"";
  int idx = json.indexOf(pattern);
  if (idx < 0) return 0;

  // Find the colon after filename
  int colonIdx = json.indexOf(":", idx);
  if (colonIdx < 0) return 0;

  // Parse the number after colon
  int start = colonIdx + 1;
  while (start < (int)json.length() && (json[start] == ' ' || json[start] == '\t')) start++;

  return (size_t)json.substring(start).toInt();
}

// Compare version strings (e.g., "1.0" < "1.1" < "2.0")
// Returns: -1 if v1 < v2, 0 if equal, 1 if v1 > v2
static int storage_compareVersions(const String& v1, const String& v2) {
  int major1 = 0, minor1 = 0, patch1 = 0;
  int major2 = 0, minor2 = 0, patch2 = 0;

  sscanf(v1.c_str(), "%d.%d.%d", &major1, &minor1, &patch1);
  sscanf(v2.c_str(), "%d.%d.%d", &major2, &minor2, &patch2);

  if (major1 != major2) return major1 < major2 ? -1 : 1;
  if (minor1 != minor2) return minor1 < minor2 ? -1 : 1;
  if (patch1 != patch2) return patch1 < patch2 ? -1 : 1;
  return 0;
}

// Read local webui version from SD card (string like "1.0")
static String storage_getLocalWebuiVersion() {
  if (!g_sdReady) return "0.0";
  if (!sd.exists(LOCAL_WEBUI_VERSION_FILE)) return "0.0";

  FsFile f = sd.open(LOCAL_WEBUI_VERSION_FILE, O_RDONLY);
  if (!f) return "0.0";

  char buf[16] = {0};
  f.read(buf, sizeof(buf) - 1);
  f.close();

  String ver = String(buf);
  ver.trim();
  return ver.length() > 0 ? ver : "0.0";
}

// Save local webui version to SD
static void storage_saveLocalWebuiVersion(const String& version) {
  if (!g_sdReady) return;

  FsFile f = sd.open(LOCAL_WEBUI_VERSION_FILE, O_WRITE | O_CREAT | O_TRUNC);
  if (!f) return;

  f.print(version);
  f.close();
}

// Fetch webui version from firmware.json on GitHub
// Parses "webui":{"version":"X.Y", "files":{"name":size,...}}
// Also populates g_webFileSizes array with expected sizes
static String storage_getRemoteWebuiVersion(bool wifiUp) {
  if (!wifiUp) return "";

  WiFiClientSecure client;
  // Skip SSL verification for now (GitHub certs change frequently)
  client.setInsecure();
  client.setTimeout(10);

  HTTPClient http;
  http.setTimeout(10000);

  Serial.print("[SD] Fetching firmware.json...");

  if (!http.begin(client, FIRMWARE_JSON_URL)) {
    Serial.println(" begin failed");
    return "";
  }

  int code = http.GET();
  if (code != 200) {
    Serial.printf(" HTTP %d\n", code);
    http.end();
    return "";
  }

  String json = http.getString();
  http.end();
  Serial.println(" OK");

  // Simple parse: find "webui" then "version":"X.Y"
  int webuiIdx = json.indexOf("\"webui\"");
  if (webuiIdx < 0) return "";

  int versionIdx = json.indexOf("\"version\"", webuiIdx);
  if (versionIdx < 0) return "";

  // Find the opening quote of the value
  int quoteStart = json.indexOf("\"", versionIdx + 9);  // skip "version"
  if (quoteStart < 0) return "";

  int quoteEnd = json.indexOf("\"", quoteStart + 1);
  if (quoteEnd < 0) return "";

  String version = json.substring(quoteStart + 1, quoteEnd);

  // Parse expected file sizes from "files" section
  for (int i = 0; i < WEB_FILES_COUNT; i++) {
    g_webFileSizes[i] = storage_parseFileSize(json, WEB_FILES[i]);
    Serial.printf("[SD] Expected %s: %u bytes\n", WEB_FILES[i], (unsigned)g_webFileSizes[i]);
  }

  return version;
}

static void storage_ensureWebUI(bool wifiUp) {
  if (!g_sdReady) return;
  storage_mkdirs();

  bool needsDownload = false;
  bool filesExist = false;

  // Check if files exist
  if (!sd.exists(WEB_INDEX)) {
    Serial.println("[SD] web UI missing");
    needsDownload = true;
  } else {
    FsFile f = sd.open(WEB_INDEX, O_RDONLY);
    if (f) {
      size_t sz = f.size();
      f.close();
      if (sz < 100) {
        Serial.println("[SD] web UI too small, re-downloading");
        needsDownload = true;
      } else {
        filesExist = true;
      }
    }
  }

  // Always check for version update if WiFi is available
  // This handles the case where old webui exists but has no .version file
  String remoteVer = "";
  if (wifiUp) {
    String localVer = storage_getLocalWebuiVersion();
    remoteVer = storage_getRemoteWebuiVersion(wifiUp);

    Serial.printf("[SD] WebUI version: local=%s, remote=%s\n", localVer.c_str(), remoteVer.c_str());

    // If no local version file exists (returns "0.0"), always update
    // Or if remote version is newer than local
    if (remoteVer.length() > 0) {
      if (localVer == "0.0" && filesExist) {
        // Old webui without version tracking - force update
        Serial.println("[SD] No version file found, updating WebUI...");
        needsDownload = true;
      } else if (storage_compareVersions(localVer, remoteVer) < 0) {
        Serial.println("[SD] New WebUI version available!");
        needsDownload = true;
      }
    }
  }

  if (!needsDownload) {
    Serial.println("[SD] web UI up to date");
    return;
  }

  if (!wifiUp) {
    Serial.println("[SD] cannot download web UI (no WiFi)");
    return;
  }

  Serial.println("[SD] downloading web UI files...");

  // Retry entire download+verify cycle up to 3 times
  for (int cycle = 0; cycle < 3; cycle++) {
    if (cycle > 0) {
      Serial.printf("[SD] WebUI download cycle %d...\n", cycle + 1);
      delay(3000);  // Wait before retry cycle
    }

    // Download all web files (with delay between to avoid connection issues)
    for (int i = 0; i < WEB_FILES_COUNT; i++) {
      storage_downloadWebFile(WEB_FILES[i], wifiUp);
      delay(1000);  // Wait between downloads to let connection close properly
    }

    // Verify all files exist and have exact expected size
    int successCount = 0;
    for (int i = 0; i < WEB_FILES_COUNT; i++) {
      char path[32];
      snprintf(path, sizeof(path), "/web/%s", WEB_FILES[i]);
      FsFile f = sd.open(path, O_RDONLY);
      if (f) {
        size_t actualSize = f.size();
        size_t expectedSize = g_webFileSizes[i];
        f.close();

        if (expectedSize > 0 && actualSize == expectedSize) {
          successCount++;
          Serial.printf("[SD] %s verified: %u bytes\n", WEB_FILES[i], (unsigned)actualSize);
        } else {
          Serial.printf("[SD] %s size mismatch: got %u, expected %u\n",
                        WEB_FILES[i], (unsigned)actualSize, (unsigned)expectedSize);
        }
      } else {
        Serial.printf("[SD] %s missing\n", WEB_FILES[i]);
      }
    }

    // If all files verified, save version and return
    if (successCount == WEB_FILES_COUNT) {
      if (remoteVer.length() == 0) {
        remoteVer = storage_getRemoteWebuiVersion(wifiUp);
      }
      if (remoteVer.length() > 0) {
        storage_saveLocalWebuiVersion(remoteVer);
        Serial.printf("[SD] WebUI updated to version %s\n", remoteVer.c_str());
      }
      return;  // Success!
    }

    Serial.printf("[SD] WebUI update incomplete: %d/%d files\n", successCount, WEB_FILES_COUNT);
  }

  // All cycles failed - delete version file to force re-download next boot
  Serial.println("[SD] WebUI update failed after all retries");
  if (sd.exists(LOCAL_WEBUI_VERSION_FILE)) {
    sd.remove(LOCAL_WEBUI_VERSION_FILE);
  }
}
