#include "yandex.h"
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <LittleFS.h>
#include "secrets.h"

// Лог в LittleFS
static void ydLogResponse(const char *stage, int code, const String &body) {
  File f = LittleFS.open("/yd_log.json", "w");
  if (!f) return;
  f.printf("{\"stage\":\"%s\",\"code\":%d,\"len\":%d,\"body\":\"", stage, code, body.length());
  for (size_t i=0; i<body.length(); i++) {
    char c = body[i];
    if (c=='\"') f.print("\\\"");
    else if (c=='\\') f.print("\\\\");
    else if ((uint8_t)c < 32) {} else f.print(c);
  }
  f.print("\"}");
  f.close();
}

static void httpBegin(HTTPClient &http, WiFiClientSecure &client, const String &url) {
  client.setInsecure();
  http.setTimeout(15000);
  http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
  http.begin(client, url);
  http.addHeader("Authorization", String("OAuth ") + YD_OAUTH_TOKEN);
}

// Проверка существования ресурса (файл или папка)
bool ydResourceExists(const String &path) {
  WiFiClientSecure client;
  HTTPClient http;
  String url = "https://cloud-api.yandex.net/v1/disk/resources?path=" + path;
  httpBegin(http, client, url);
  int code = http.GET();
  String body = http.getString();
  http.end();
  ydLogResponse("exists", code, body);
  return (code == 200);
}

// Создание папки (оставляем как было)
bool ydCreateFolder(const String &dir) {
  WiFiClientSecure client;
  HTTPClient http;
  String url = "https://cloud-api.yandex.net/v1/disk/resources?path=" + dir;
  httpBegin(http, client, url);
  int code = http.sendRequest("PUT");
  String body = http.getString();
  http.end();
  ydLogResponse("mkdir", code, body);
  return (code >= 200 && code < 300) || (code == 409);
}

// Получить href для загрузки
static bool ydGetUploadHref(const String &remotePath, String &outHref, bool overwrite) {
  WiFiClientSecure client;
  HTTPClient http;
  String url = "https://cloud-api.yandex.net/v1/disk/resources/upload?path=" + remotePath +
               (overwrite ? "&overwrite=true" : "&overwrite=false");
  httpBegin(http, client, url);
  int code = http.GET();
  String body = http.getString();
  http.end();
  ydLogResponse("getHref", code, body);
  if (code != 200) return false;

  int i = body.indexOf("\"href\":\"");
  if (i < 0) return false;
  i += 8;
  int j = body.indexOf("\"", i);
  if (j <= i) return false;
  outHref = body.substring(i, j);
  outHref.replace("\\/", "/");
  return true;
}

// Загрузка файла по href
static bool ydPutFileToHref(const String &href, const String &localPath, int &outCode, String &outBody) {
  File f = LittleFS.open(localPath, "r");
  if (!f) { outCode = -1; outBody = "local open fail"; return false; }
  WiFiClientSecure client;
  HTTPClient http;
  httpBegin(http, client, href); // Authorization останется, не мешает
  outCode = http.sendRequest("PUT", &f, f.size());
  outBody = http.getString();
  http.end();
  f.close();
  ydLogResponse("upload", outCode, outBody);
  return (outCode >= 200 && outCode < 300);
}

// БЫЛО (оставляем совместимость): перезапись
bool ydEnsureUpload(const String &remoteDir, const String &localPath, const String &remoteFile) {
  // убеждаемся, что базовая папка существует (по желанию можно раскомментировать вызов mkdir)
  // if (!ydCreateFolder(remoteDir)) return false;

  String remotePath = remoteDir + "/" + remoteFile;
  String href;
  if (!ydGetUploadHref(remotePath, href, true)) return false;
  int code = 0; String body;
  return ydPutFileToHref(href, localPath, code, body);
}

// НОВОЕ: загрузка без перезаписи — автопереименование при дублях
bool ydEnsureUploadNoOverwrite(const String &remoteDir,
                               const String &localPath,
                               const String &baseFileName,
                               String &outRemotePath) {
  // 1) Убедиться, что папка есть (или создана)
  if (!ydCreateFolder(remoteDir)) {
    // если уже есть — вернётся ok (409 считаем успехом)
    // если прямой отказ — всё равно попробуем, но лучше вернуть false
    // вернём false, чтобы быть строже
    return false;
  }

  // 2) Подобрать уникальное имя файла
  String name = baseFileName;   // например, "cat.jpg"
  int dot = name.lastIndexOf('.');
  String base = (dot > 0) ? name.substring(0, dot) : name;
  String ext  = (dot > 0) ? name.substring(dot)     : "";

  String candidate = remoteDir + "/" + name;
  int suffix = 1;
  while (ydResourceExists(candidate)) {
    candidate = remoteDir + "/" + base + "(" + String(suffix++) + ")" + ext;
  }

  // 3) Получить href с overwrite=false и залить
  String href;
  if (!ydGetUploadHref(candidate, href, /*overwrite=*/false)) return false;

  int code = 0; String body;
  bool ok = ydPutFileToHref(href, localPath, code, body);
  if (ok) outRemotePath = candidate;
  return ok;
}

bool ydListFolder(const String &dir, String &outJson) {
  WiFiClientSecure client;
  HTTPClient http;
  String url = "https://cloud-api.yandex.net/v1/disk/resources?path=" + dir + "&limit=100&fields=_embedded.items.name";
  httpBegin(http, client, url);

  int code = http.GET();
  outJson = http.getString();
  http.end();

  ydLogResponse("list", code, outJson);
  return (code == 200);
}
