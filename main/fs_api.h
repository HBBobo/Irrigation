#pragma once
#include <WebServer.h>
#include <SdFat.h>

extern SdFat sd;

// JSON escape function to prevent injection
static String jsonEscape(const String& s) {
  String out;
  out.reserve(s.length() + 10);
  for (unsigned int i = 0; i < s.length(); i++) {
    char c = s[i];
    switch (c) {
      case '"':  out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n";  break;
      case '\r': out += "\\r";  break;
      case '\t': out += "\\t";  break;
      default:   out += c;      break;
    }
  }
  return out;
}

// Path sanitization to prevent directory traversal attacks
static String sanitizePath(const String& path) {
  String clean = path;

  // Remove dangerous sequences
  while (clean.indexOf("..") >= 0) {
    clean.replace("..", "");
  }

  // Ensure starts with /
  if (!clean.startsWith("/")) {
    clean = "/" + clean;
  }

  // Remove double slashes
  while (clean.indexOf("//") >= 0) {
    clean.replace("//", "/");
  }

  // Remove trailing slash (except for root)
  if (clean.length() > 1 && clean.endsWith("/")) {
    clean = clean.substring(0, clean.length() - 1);
  }

  return clean;
}

static void fs_handleList(WebServer& srv) {
  if (!srv.hasArg("path")) {
    srv.send(400, "application/json", "{\"error\":\"missing path parameter\"}");
    return;
  }

  String rawPath = srv.arg("path");
  String path = sanitizePath(rawPath);

  FsFile dir = sd.open(path.c_str());
  if (!dir || !dir.isDirectory()) {
    dir.close();
    srv.send(404, "application/json", "{\"error\":\"not a directory\"}");
    return;
  }

  // Use fixed buffer to reduce memory fragmentation
  char json[2048];
  int pos = 0;

  pos += snprintf(json + pos, sizeof(json) - pos, "{\"path\":\"%s\",\"items\":[", jsonEscape(path).c_str());

  bool first = true;
  FsFile f;
  int itemCount = 0;
  const int maxItems = 50;  // Limit number of items to prevent memory issues

  while ((f = dir.openNextFile()) && itemCount < maxItems) {
    char name[64];
    f.getName(name, sizeof(name));

    if (!first && pos < (int)sizeof(json) - 100) {
      json[pos++] = ',';
    }
    first = false;

    // Add item to JSON
    int written = snprintf(json + pos, sizeof(json) - pos,
      "{\"name\":\"%s\",\"size\":%lu,\"dir\":%s}",
      jsonEscape(String(name)).c_str(),
      (unsigned long)f.size(),
      f.isDirectory() ? "true" : "false"
    );

    if (written > 0 && pos + written < (int)sizeof(json) - 10) {
      pos += written;
    }

    f.close();
    itemCount++;
  }

  dir.close();

  // Close JSON array and object
  pos += snprintf(json + pos, sizeof(json) - pos, "]}");

  srv.send(200, "application/json", json);
}

// Download file
static void fs_handleDownload(WebServer& srv) {
  if (!srv.hasArg("path")) {
    srv.send(400, "text/plain", "missing path");
    return;
  }

  String path = sanitizePath(srv.arg("path"));
  FsFile f = sd.open(path.c_str(), O_RDONLY);
  if (!f || f.isDirectory()) {
    if (f) f.close();
    srv.send(404, "text/plain", "file not found");
    return;
  }

  size_t fileSize = f.size();

  // Read entire file into buffer (limit to reasonable size)
  if (fileSize > 65536) {
    f.close();
    srv.send(413, "text/plain", "file too large");
    return;
  }

  uint8_t* buf = (uint8_t*)malloc(fileSize);
  if (!buf) {
    f.close();
    srv.send(500, "text/plain", "out of memory");
    return;
  }

  size_t bytesRead = f.read(buf, fileSize);
  f.close();

  // Send complete response at once
  srv.sendHeader("Content-Disposition", "attachment; filename=\"" + path.substring(path.lastIndexOf('/') + 1) + "\"");
  srv.send_P(200, "application/octet-stream", (const char*)buf, bytesRead);

  free(buf);
}

// Delete file or empty directory
static void fs_handleDelete(WebServer& srv) {
  if (!srv.hasArg("path")) {
    srv.send(400, "application/json", "{\"error\":\"missing path\"}");
    return;
  }

  String path = sanitizePath(srv.arg("path"));

  // Prevent deleting critical files
  if (path == "/" || path == "/web" || path == "/cfg.txt") {
    srv.send(403, "application/json", "{\"error\":\"cannot delete protected path\"}");
    return;
  }

  if (!sd.exists(path.c_str())) {
    srv.send(404, "application/json", "{\"error\":\"not found\"}");
    return;
  }

  FsFile f = sd.open(path.c_str());
  bool isDir = f.isDirectory();
  f.close();

  bool ok;
  if (isDir) {
    ok = sd.rmdir(path.c_str());
  } else {
    ok = sd.remove(path.c_str());
  }

  if (ok) {
    srv.send(200, "application/json", "{\"ok\":true}");
  } else {
    srv.send(500, "application/json", "{\"error\":\"delete failed\"}");
  }
}

// Upload file (supports chunked uploads)
static void fs_handleUpload(WebServer& srv) {
  if (!srv.hasArg("path")) {
    srv.send(400, "application/json", "{\"error\":\"missing path\"}");
    return;
  }

  String path = sanitizePath(srv.arg("path"));
  bool append = srv.hasArg("append") && srv.arg("append") == "1";

  // Get raw body data
  if (srv.hasArg("plain")) {
    String body = srv.arg("plain");

    FsFile f = sd.open(path.c_str(), append ? (O_WRITE | O_CREAT | O_APPEND) : (O_WRITE | O_CREAT | O_TRUNC));
    if (!f) {
      srv.send(500, "application/json", "{\"error\":\"cannot open file\"}");
      return;
    }

    f.write((const uint8_t*)body.c_str(), body.length());
    size_t sz = f.size();
    f.close();

    char json[64];
    snprintf(json, sizeof(json), "{\"ok\":true,\"size\":%lu}", (unsigned long)sz);
    srv.send(200, "application/json", json);
  } else {
    srv.send(400, "application/json", "{\"error\":\"no data\"}");
  }
}

// Create directory
static void fs_handleMkdir(WebServer& srv) {
  if (!srv.hasArg("path")) {
    srv.send(400, "application/json", "{\"error\":\"missing path\"}");
    return;
  }

  String path = sanitizePath(srv.arg("path"));

  if (sd.exists(path.c_str())) {
    srv.send(409, "application/json", "{\"error\":\"already exists\"}");
    return;
  }

  if (sd.mkdir(path.c_str())) {
    srv.send(200, "application/json", "{\"ok\":true}");
  } else {
    srv.send(500, "application/json", "{\"error\":\"mkdir failed\"}");
  }
}

static void fs_register(WebServer& srv) {
  srv.on("/api/fs/list", HTTP_GET, [&]() {
    fs_handleList(srv);
  });
  srv.on("/api/fs/download", HTTP_GET, [&]() {
    fs_handleDownload(srv);
  });
  srv.on("/api/fs/delete", HTTP_POST, [&]() {
    fs_handleDelete(srv);
  });
  srv.on("/api/fs/delete", HTTP_GET, [&]() {
    fs_handleDelete(srv);  // Allow GET for easy testing
  });
  srv.on("/api/fs/upload", HTTP_POST, [&]() {
    fs_handleUpload(srv);
  });
  srv.on("/api/fs/mkdir", HTTP_POST, [&]() {
    fs_handleMkdir(srv);
  });
}
