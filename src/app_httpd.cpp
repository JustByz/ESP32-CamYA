#include "esp_camera.h"
#include <WiFi.h>
#include "esp_http_server.h"
#include <FS.h>
#include <LittleFS.h>
#include "yandex.h"
#include "Config.h"
#include <ArduinoJson.h>
#include "camera_index.h"
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

// ───────────────────────────────
// GET-параметры
// ───────────────────────────────
static esp_err_t parse_get(httpd_req_t *req, char **out_query) {
  size_t buf_len = httpd_req_get_url_query_len(req) + 1;
  if (buf_len > 1) {
    *out_query = (char *)malloc(buf_len);
    if (!*out_query) return ESP_ERR_NO_MEM;
    if (httpd_req_get_url_query_str(req, *out_query, buf_len) == ESP_OK) return ESP_OK;
    free(*out_query);
  }
  return ESP_FAIL;
}

#define httpd_resp_send_400(req) httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Некорректный запрос")

// ───────────── / → главная ─────────────
static esp_err_t index_handler(httpd_req_t *req) {
  httpd_resp_set_type(req, "text/html");
  return httpd_resp_send(req, index_html, strlen(index_html));
}

// ───────────── /routes → список доступных URI ─────────────
static esp_err_t routes_handler(httpd_req_t *req) {
  static const char json[] =
    "{\"routes\":[\"/\",\"/routes\",\"/ytest\",\"/ydlog\",\"/capture\",\"/save\",\"/mkdir\",\"/stream(:81)\"]}";
  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  return httpd_resp_send(req, json, sizeof(json) - 1);
}

// ───────────── /ydlog → последний лог API (JSON) ─────────────
static esp_err_t ydlog_handler(httpd_req_t *req) {
  if (!LittleFS.exists("/yd_log.json")) {
    httpd_resp_send_404(req);
    return ESP_FAIL;
  }
  File f = LittleFS.open("/yd_log.json", "r");
  if (!f) { httpd_resp_send_500(req); return ESP_FAIL; }
  String content = f.readString(); f.close();
  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  return httpd_resp_send(req, content.c_str(), content.length());
}

// ───────────── /ytest → проверка API Я.Диск (устойчивый HTTPS) ─────────────
static esp_err_t ytest_handler(httpd_req_t *req) {
  WiFiClientSecure client;     // ← на стеке, без new
  client.setInsecure();
  HTTPClient http;
  http.setTimeout(10000);
  http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
  http.begin(client, "https://cloud-api.yandex.net/v1/disk");
  http.addHeader("Authorization", "OAuth " YD_OAUTH_TOKEN);

  int code = http.GET();
  String body = http.getString();
  http.end();

  // Попробуем сохранить лог (если FS не смонтирован — просто пропустим)
  File f = LittleFS.open("/yd_log.json", "w");
  if (f) {
    StaticJsonDocument<768> j;
    j["stage"] = "ytest";
    j["code"]  = code;
    j["body"]  = body;
    serializeJson(j, f);
    f.close();
  }

  StaticJsonDocument<96> doc;
  doc["code"] = code;
  doc["len"]  = body.length();
  String out; serializeJson(doc, out);

  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  return httpd_resp_send(req, out.c_str(), out.length());
}

// ───────────── /capture → фото ─────────────
static esp_err_t capture_handler(httpd_req_t *req) {
  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) { httpd_resp_send_500(req); return ESP_FAIL; }
  httpd_resp_set_type(req, "image/jpeg");
  httpd_resp_set_hdr(req, "Content-Disposition", "inline; filename=capture.jpg");
  httpd_resp_send(req, (const char *)fb->buf, fb->len);
  esp_camera_fb_return(fb);
  return ESP_OK;
}

// ───────────── /stream → поток с корректным выходом ─────────────
static esp_err_t stream_handler(httpd_req_t *req) {
  camera_fb_t *fb = NULL;
  esp_err_t res = ESP_OK;
  char part_buf[64];

  static const char* CT = "multipart/x-mixed-replace;boundary=frame";
  static const char* B  = "\r\n--frame\r\n";
  static const char* P  = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

  httpd_resp_set_type(req, CT);

  while (true) {
    fb = esp_camera_fb_get();
    if (!fb) { res = ESP_FAIL; break; }
    if (fb->format != PIXFORMAT_JPEG) { res = ESP_FAIL; esp_camera_fb_return(fb); break; }
    if (httpd_resp_send_chunk(req, B, strlen(B)) != ESP_OK) { esp_camera_fb_return(fb); break; }
    snprintf(part_buf, 64, P, fb->len);
    if (httpd_resp_send_chunk(req, part_buf, strlen(part_buf)) != ESP_OK) { esp_camera_fb_return(fb); break; }
    if (httpd_resp_send_chunk(req, (const char *)fb->buf, fb->len) != ESP_OK) { esp_camera_fb_return(fb); break; }
    esp_camera_fb_return(fb);
  }
  return res;
}

// ───────────── /save → фото на Я.Диск ─────────────
static esp_err_t save_handler(httpd_req_t *req) {
  char *buf = NULL; char name[64] = {0};
  if (parse_get(req, &buf) == ESP_OK) { httpd_query_key_value(buf, "name", name, sizeof(name)); free(buf); }

  camera_fb_t* fb = esp_camera_fb_get();
  if (!fb) { httpd_resp_send_500(req); return ESP_FAIL; }

  String baseName = (strlen(name) > 0) ? String(name) : "photo";
  String local = "/" + baseName + ".jpg";

  File f = LittleFS.open(local, "w");
  if (!f) { esp_camera_fb_return(fb); httpd_resp_send_500(req); return ESP_FAIL; }
  f.write(fb->buf, fb->len); f.close();
  size_t sz = fb->len; esp_camera_fb_return(fb);

  String remoteBase = String(YD_BASE_DIR) + "/" + baseName + ".jpg";
  String remote = ydUploadFileWithRename(local, remoteBase);
  LittleFS.remove(local);

  StaticJsonDocument<256> doc;
  doc["ok"]     = (remote != "");
  doc["remote"] = remote;
  doc["bytes"]  = sz;

  String out; serializeJson(doc, out);
  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  return httpd_resp_send(req, out.c_str(), out.length());
}

// ───────────── /mkdir → создать папку ─────────────
static esp_err_t mkdir_handler(httpd_req_t *req) {
  char *buf = NULL; char name[96] = {0};
  if (parse_get(req, &buf) == ESP_OK) { httpd_query_key_value(buf, "name", name, sizeof(name)); free(buf); }
  if (strlen(name) == 0) { httpd_resp_send_400(req); return ESP_FAIL; }

  String remoteDir = String(YD_BASE_DIR) + "/" + String(name);
  bool ok = ydEnsureDirsREST(remoteDir);

  StaticJsonDocument<160> doc;
  doc["ok"]  = ok;
  doc["dir"] = remoteDir;

  String out; serializeJson(doc, out);
  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  return httpd_resp_send(req, out.c_str(), out.length());
}

// ───────────── Запуск серверов ─────────────
void startCameraServer() {
  // HTTP (порт 80)
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.recv_wait_timeout = 30;
  config.send_wait_timeout = 30;
  httpd_handle_t server = NULL;
  if (httpd_start(&server, &config) == ESP_OK) {
    httpd_uri_t index_uri   = { .uri="/",       .method=HTTP_GET, .handler=index_handler,  .user_ctx=NULL };
    httpd_uri_t routes_uri  = { .uri="/routes", .method=HTTP_GET, .handler=routes_handler, .user_ctx=NULL };
    httpd_uri_t log_uri     = { .uri="/ydlog",  .method=HTTP_GET, .handler=ydlog_handler,  .user_ctx=NULL };
    httpd_uri_t ytest_uri   = { .uri="/ytest",  .method=HTTP_GET, .handler=ytest_handler,  .user_ctx=NULL };
    httpd_uri_t capture_uri = { .uri="/capture",.method=HTTP_GET, .handler=capture_handler,.user_ctx=NULL };
    httpd_uri_t save_uri    = { .uri="/save",   .method=HTTP_GET, .handler=save_handler,   .user_ctx=NULL };
    httpd_uri_t mkdir_uri   = { .uri="/mkdir",  .method=HTTP_GET, .handler=mkdir_handler,  .user_ctx=NULL };

    httpd_register_uri_handler(server, &index_uri);
    httpd_register_uri_handler(server, &routes_uri);
    httpd_register_uri_handler(server, &log_uri);
    httpd_register_uri_handler(server, &ytest_uri);
    httpd_register_uri_handler(server, &capture_uri);
    httpd_register_uri_handler(server, &save_uri);
    httpd_register_uri_handler(server, &mkdir_uri);
  }

  // Стрим (порт 81)
  httpd_config_t config_stream = HTTPD_DEFAULT_CONFIG();
  config_stream.server_port = 81;
  config_stream.ctrl_port   = 32769;
  config_stream.recv_wait_timeout = 30;
  config_stream.send_wait_timeout = 30;
  httpd_handle_t stream_httpd = NULL;
  if (httpd_start(&stream_httpd, &config_stream) == ESP_OK) {
    httpd_uri_t stream_uri = { .uri="/stream", .method=HTTP_GET, .handler=stream_handler, .user_ctx=NULL };
    httpd_register_uri_handler(stream_httpd, &stream_uri);
  }
}
                       