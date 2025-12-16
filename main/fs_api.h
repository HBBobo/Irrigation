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

  // Whitelist: only allow specific directories
  // Allow: /web/, /log/, and root listing
  if (clean == "/" ||
      clean.startsWith("/web/") || clean == "/web" ||
      clean.startsWith("/log/") || clean == "/log") {
    return clean;
  }

  // Default to root for unauthorized paths
  return "/";
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

static void fs_register(WebServer& srv) {
  srv.on("/api/fs/list", HTTP_GET, [&]() {
    fs_handleList(srv);
  });
}
