#include "yandex.h"
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <FS.h>           // 👈 добавлено
#include <LittleFS.h>     // 👈 встроенный LittleFS из core
#include <ArduinoJson.h>
#include "Config.h"

static const uint16_t YD_HTTP_TIMEOUT = 15000;

static void ydLogResponse(const String &stage, int code, const String &body) {
  File f = LittleFS.open("/yd_log.json", "w");
  if (!f) return;
  StaticJsonDocument<768> doc;
  doc["stage"] = stage;
  doc["code"]  = code;
  doc["body"]  = body;
  serializeJson(doc, f);
  f.close();
}

static inline bool isUnreserved(char c) {
  return (c >= 'a' && c <= 'z') ||
         (c >= 'A' && c <= 'Z') ||
         (c >= '0' && c <= '9') ||
         c == '-' || c == '.' || c == '_' || c == '~';
}

String ydUrlEncode(const String &s) {
  String out; out.reserve(s.length() * 3);
  for (size_t i = 0; i < s.length(); i++) {
    uint8_t b = (uint8_t)s[i];
    if (isUnreserved((char)b)) out += (char)b;
    else { char hex[4]; snprintf(hex, sizeof(hex), "%%%02X", b); out += hex; }
  }
  return out;
}

static bool ensureWiFi() {
  if (WiFi.status() == WL_CONNECTED) return true;
  ydLogResponse("wifi", -1, "WiFi not connected");
  return false;
}

static void httpBeginSecure(HTTPClient &http, const String &url) {
  WiFiClientSecure *client = new WiFiClientSecure;
  client->setInsecure();
  client->setTimeout(YD_HTTP_TIMEOUT);
  http.setTimeout(YD_HTTP_TIMEOUT);
  http.setReuse(false);
  http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
  http.begin(*client, url);
}

static bool ydResourceExists(const String &path) {
  if (!ensureWiFi()) return false;
  HTTPClient http;
  const String url = "https://cloud-api.yandex.net/v1/disk/resources?path=" + ydUrlEncode(path);
  httpBeginSecure(http, url);
  http.addHeader("Authorization", "OAuth " YD_OAUTH_TOKEN);
  const int code = http.GET();
  String body = http.getString();
  http.end();
  ydLogResponse(String("exists:") + path, code, body);
  return (code == 200);
}

static String getUploadHref(const String &remotePath) {
  if (!ensureWiFi()) return "";
  HTTPClient http;
  const String url = "https://cloud-api.yandex.net/v1/disk/resources/upload?path=" +
                     ydUrlEncode(remotePath) + "&overwrite=true";
  httpBeginSecure(http, url);
  http.addHeader("Authorization", "OAuth " YD_OAUTH_TOKEN);
  int code = http.GET();
  String href;
  String body = http.getString();
  http.end();
  if (code == 200) {
    int i = body.indexOf("\"href\":\"");
    if (i >= 0) { i += 8; int j = body.indexOf("\"", i); href = body.substring(i, j); }
  }
  ydLogResponse(String("getHref:") + remotePath, code, body);
  return href;
}

bool ydUploadFile(const String &localPath, const String &remotePath) {
  if (!ensureWiFi()) return false;
  const String href = getUploadHref(remotePath);
  if (href == "") { ydLogResponse("upload:nohref", -2, "empty href"); return false; }
  File f = LittleFS.open(localPath, "r");
  if (!f) { ydLogResponse(String("upload:openfail:") + localPath, -3, "open fail"); return false; }
  HTTPClient http;
  httpBeginSecure(http, href);
  const int code = http.sendRequest("PUT", &f, f.size());
  String body = http.getString();
  http.end();
  f.close();
  ydLogResponse(String("upload:") + remotePath, code, body);
  return (code >= 200 && code < 300);
}

String ydUploadFileWithRename(const String &localPath, const String &remotePath) {
  String target = remotePath;
  int counter = 1;
  while (ydResourceExists(target)) {
    int dot = remotePath.lastIndexOf(".");
    const String base = (dot > 0) ? remotePath.substring(0, dot) : remotePath;
    const String ext  = (dot > 0) ? remotePath.substring(dot) : "";
    target = base + "(" + String(counter++) + ")" + ext;
  }
  const bool ok = ydUploadFile(localPath, target);
  return ok ? target : "";
}

static bool ydEnsureOneDir(const String &dir) {
  if (!ensureWiFi()) return false;
  HTTPClient http;
  const String url = "https://cloud-api.yandex.net/v1/disk/resources?path=" + ydUrlEncode(dir);
  httpBeginSecure(http, url);
  http.addHeader("Authorization", "OAuth " YD_OAUTH_TOKEN);
  const int code = http.PUT("");
  String body = http.getString();
  http.end();
  ydLogResponse(String("mkdir:") + dir, code, body);
  if (code == 409) return true;
  if (code >= 200 && code < 300) return true;
  return false;
}

bool ydEnsureDirsREST(const String &dir) {
  if (!ydEnsureOneDir(String(YD_BASE_DIR))) return false;
  if (dir == String(YD_BASE_DIR)) return true;
  return ydEnsureOneDir(dir);
}
