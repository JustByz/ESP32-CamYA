#include "yandex.h"
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <FS.h>
#include <LittleFS.h>
#include <ArduinoJson.h>

// Логирование в LittleFS
static void ydLogResponse(const char *stage, int code, const String &body) {
  File f = LittleFS.open("/yd_log.json", "w+");
  if (!f) return;
  StaticJsonDocument<768> j;
  j["stage"] = stage;
  j["code"]  = code;
  j["body"]  = body;
  serializeJson(j, f);
  f.close();
}

// Проверка ресурса (файла или папки)
bool ydResourceExists(const String &path) {
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  String url = "https://cloud-api.yandex.net/v1/disk/resources?path=" + path;
  http.begin(client, url);
  http.addHeader("Authorization", String("OAuth ") + YD_OAUTH_TOKEN);

  int code = http.GET();
  String body = http.getString();
  http.end();

  ydLogResponse("exists", code, body);
  return (code == 200);
}

// Создание папки
bool ydCreateFolder(const String &path) {
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  String url = "https://cloud-api.yandex.net/v1/disk/resources?path=" + path;
  http.begin(client, url);
  http.addHeader("Authorization", String("OAuth ") + YD_OAUTH_TOKEN);

  int code = http.sendRequest("PUT");
  String body = http.getString();
  http.end();

  ydLogResponse("mkdir", code, body);
  return (code == 201 || code == 409); // 201 Created, 409 Already exists
}

// Получение upload href
String ydGetUploadHref(const String &remotePath, bool overwrite) {
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  String url = "https://cloud-api.yandex.net/v1/disk/resources/upload?path=" + remotePath +
               (overwrite ? "&overwrite=true" : "&overwrite=false");

  http.begin(client, url);
  http.addHeader("Authorization", String("OAuth ") + YD_OAUTH_TOKEN);

  int code = http.GET();
  String body = http.getString();
  http.end();

  ydLogResponse("getHref", code, body);

  if (code != 200) return "";

  StaticJsonDocument<512> doc;
  if (deserializeJson(doc, body)) return "";
  return doc["href"].as<String>();
}

// Загрузка файла
bool ydUploadFile(const String &localPath, const String &remotePath) {
  String href = ydGetUploadHref(remotePath, true);
  if (href == "") return false;

  File f = LittleFS.open(localPath, "r");
  if (!f) {
    ydLogResponse("upload", 500, "cannot open local file");
    return false;
  }

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.begin(client, href);

  int code = http.sendRequest("PUT", &f, f.size());
  String body = http.getString();
  f.close();
  http.end();

  ydLogResponse("upload", code, body);
  return (code == 201 || code == 202);
}

// Удобная обёртка
bool ydEnsureUpload(const String &folder, const String &localFile, const String &remoteFile) {
  if (!ydResourceExists(folder)) {
    if (!ydCreateFolder(folder)) return false;
  }
  return ydUploadFile(localFile, folder + "/" + remoteFile);
}
