#pragma once
#include <WebServer.h>
#include <SdFat.h>

extern SdFat sd;
extern bool storage_isReady();

static bool fs_safePath(const String& p) {
  return p.startsWith("/") && p.indexOf("..") < 0;
}

static void fs_handleList(WebServer& srv) {
  if (!storage_isReady()) { srv.send(503, "text/plain", "SD not ready"); return; }

  String path = srv.arg("path");
  if (path.length() == 0) path = "/";

  if (!fs_safePath(path)) { srv.send(400, "text/plain", "Bad path"); return; }

  FsFile dir = sd.open(path.c_str());
  if (!dir || !dir.isDir()) { srv.send(404, "text/plain", "Not a dir"); return; }

  String json = "{";
  json += "\"path\":\"" + path + "\",";
  json += "\"items\":[";

  bool first = true;
  while (true) {
    FsFile f = dir.openNextFile();
    if (!f) break;

    char name[96];
    if (f.getName(name, sizeof(name)) > 0) {
      if (!first) json += ",";
      first = false;
      json += "{";
      json += "\"name\":\"" + String(name) + "\",";
      json += "\"dir\":" + String(f.isDir() ? "true" : "false") + ",";
      json += "\"size\":" + String((unsigned long)f.fileSize());
      json += "}";
    }
    f.close();
    delay(0);
  }

  json += "]}";
  dir.close();
  srv.send(200, "application/json", json);
}

static void fs_handleGet(WebServer& srv) {
  if (!storage_isReady()) { srv.send(503, "text/plain", "SD not ready"); return; }

  String path = srv.arg("path");
  if (!fs_safePath(path)) { srv.send(400, "text/plain", "Bad path"); return; }

  FsFile f = sd.open(path.c_str(), O_RDONLY);
  if (!f) { srv.send(404, "text/plain", "Not found"); return; }

  srv.setContentLength(f.fileSize());
  srv.sendHeader("Content-Type", "application/octet-stream");
  srv.send(200);

  WiFiClient c = srv.client();
  uint8_t buf[1024];
  while (f.available() && c.connected()) {
    int n = f.read(buf, sizeof(buf));
    if (n <= 0) break;
    c.write(buf, n);
    delay(0);
  }

  f.close();
}

static void fs_handleUpload(WebServer& srv) {
  static FsFile uploadFile;
  HTTPUpload& up = srv.upload();

  if (up.status == UPLOAD_FILE_START) {
    if (!storage_isReady()) return;

    String path = srv.arg("path");
    if (!fs_safePath(path)) return;

    uploadFile = sd.open(path.c_str(), O_WRITE | O_CREAT | O_TRUNC);
  }
  else if (up.status == UPLOAD_FILE_WRITE) {
    if (uploadFile) uploadFile.write(up.buf, up.currentSize);
  }
  else if (up.status == UPLOAD_FILE_END) {
    if (uploadFile) uploadFile.close();
  }
}

static void fs_register(WebServer& srv) {
  srv.on("/api/fs/list", HTTP_GET, [&]() { fs_handleList(srv); });
  srv.on("/api/fs/get",  HTTP_GET, [&]() { fs_handleGet(srv); });
  srv.on("/api/fs/upload", HTTP_POST,
    [&]() { srv.send(200, "text/plain", "OK"); },
    [&]() { fs_handleUpload(srv); }
  );
}
