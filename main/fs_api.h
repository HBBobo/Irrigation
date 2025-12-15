#pragma once
#include <Arduino.h>
#include <WebServer.h>
#include <SdFat.h>

// A storage.h-ban már léteznek ezek:
extern SdFat sd;
extern bool storage_isReady();

// ===== CONFIG =====
static const size_t FS_PAGE_SIZE = 20;   // hány elem / oldal
static const size_t FS_STREAM_BUF = 1024;

// ===== HELPERS =====
static bool fs_safePath(const String& p) {
  return p.startsWith("/") && p.indexOf("..") < 0;
}

static String fs_formatSize(uint64_t s) {
  if (s < 1024) return String(s) + " B";
  if (s < 1024ULL * 1024) return String((float)s / 1024.0f, 1) + " KB";
  if (s < 1024ULL * 1024 * 1024)
    return String((float)s / (1024.0f * 1024.0f), 1) + " MB";
  return String((float)s / (1024.0f * 1024.0f * 1024.0f), 1) + " GB";
}

// ===== LIST DIR =====
// GET /api/fs/list?path=/logs&page=0
static void fs_handleList(WebServer& srv) {
  if (!storage_isReady()) {
    srv.send(503, "text/plain", "SD not ready");
    return;
  }

  String path = srv.arg("path");
  if (path.length() == 0) path = "/";
  if (!fs_safePath(path)) {
    srv.send(400, "text/plain", "Bad path");
    return;
  }

  size_t page = srv.arg("page").toInt();
  size_t skip = page * FS_PAGE_SIZE;

  FsFile dir;
  if (!dir.open(path.c_str())) {
    srv.send(404, "text/plain", "Dir not found");
    return;
  }

  if (!dir.isDir()) {
    dir.close();
    srv.send(400, "text/plain", "Not a dir");
    return;
  }

  String json = "{";
  json += "\"path\":\"" + path + "\",";
  json += "\"page\":" + String(page) + ",";
  json += "\"items\":[";

  FsFile f;
  size_t idx = 0;
  size_t emitted = 0;

  while (f.openNext(&dir, O_RDONLY)) {
    if (idx++ < skip) {
      f.close();
      continue;
    }
    if (emitted >= FS_PAGE_SIZE) {
      f.close();
      break;
    }

    if (emitted > 0) json += ",";

    json += "{";
    json += "\"name\":\"" + String(f.getName()) + "\",";
    json += "\"dir\":" + String(f.isDir() ? "true" : "false") + ",";
    json += "\"size\":\"" + fs_formatSize(f.fileSize()) + "\"";
    json += "}";

    emitted++;
    f.close();
  }

  dir.close();
  json += "]}";

  srv.send(200, "application/json", json);
}

// ===== DOWNLOAD FILE =====
// GET /api/fs/get?path=/logs/data.csv
static void fs_handleGet(WebServer& srv) {
  if (!storage_isReady()) {
    srv.send(503, "text/plain", "SD not ready");
    return;
  }

  String path = srv.arg("path");
  if (!fs_safePath(path)) {
    srv.send(400, "text/plain", "Bad path");
    return;
  }

  FsFile f;
  if (!f.open(path.c_str(), O_RDONLY)) {
    srv.send(404, "text/plain", "File not found");
    return;
  }

  srv.setContentLength(f.fileSize());
  srv.sendHeader("Content-Type", "application/octet-stream");
  srv.sendHeader("Content-Disposition",
                 "attachment; filename=\"" + String(f.getName()) + "\"");
  srv.send(200);

  uint8_t buf[FS_STREAM_BUF];
  WiFiClient client = srv.client();

  while (f.available() && client.connected()) {
    size_t n = f.read(buf, sizeof(buf));
    if (n == 0) break;
    client.write(buf, n);
    delay(0); // yield
  }

  f.close();
}

// ===== UPLOAD FILE =====
// POST /api/fs/upload?path=/web/index.html
static void fs_handleUpload(WebServer& srv) {
  static FsFile uploadFile;

  HTTPUpload& up = srv.upload();

  if (up.status == UPLOAD_FILE_START) {
    if (!storage_isReady()) return;

    String path = srv.arg("path");
    if (!fs_safePath(path)) return;

    uploadFile.open(path.c_str(), O_WRITE | O_CREAT | O_TRUNC);
  }
  else if (up.status == UPLOAD_FILE_WRITE) {
    if (uploadFile) {
      uploadFile.write(up.buf, up.currentSize);
    }
  }
  else if (up.status == UPLOAD_FILE_END) {
    if (uploadFile) {
      uploadFile.close();
    }
  }
}

// ===== REGISTER =====
static void fs_register(WebServer& srv) {
  srv.on("/api/fs/list", HTTP_GET, [&]() {
    fs_handleList(srv);
  });

  srv.on("/api/fs/get", HTTP_GET, [&]() {
    fs_handleGet(srv);
  });

  srv.on(
    "/api/fs/upload",
    HTTP_POST,
    [&]() { srv.send(200, "text/plain", "OK"); },
    [&]() { fs_handleUpload(srv); }
  );
}
