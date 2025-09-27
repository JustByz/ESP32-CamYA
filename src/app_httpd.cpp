#include "app_httpd.h"
#include "esp_http_server.h"
#include "esp_camera.h"

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <FS.h>
#include <LittleFS.h>
#include <Update.h>

#include "yandex.h"        // ydEnsureUploadNoOverwrite, ydCreateFolder, ydListFolder, ...
#include "Config.h"
#include "camera_index.h"  // index_html (веб UI)

// ─────────────────────────────────────────────
// Версия сборки (видна на /version)
#ifndef FW_VERSION
#define FW_VERSION __DATE__ " " __TIME__
#endif

// Глобальные хэндлы серверов (порт 80 и 81)
static httpd_handle_t g_http_server   = nullptr;
static httpd_handle_t g_stream_server = nullptr;

// ─────────────────────────────────────────────
// Утилиты
static inline esp_err_t send_json(httpd_req_t *req, const String &s) {
  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  return httpd_resp_send(req, s.c_str(), s.length());
}

static void yd_write_min_log(const char *stage, int code, const String &body) {
  File f = LittleFS.open("/yd_log.json", "w");
  if (!f) return;
  f.print("{\"stage\":\"");
  for (const char *p = stage; *p; ++p) f.print(*p=='\"' ? "\\\"" : String(*p));
  f.print("\",\"code\":"); f.print(code);
  f.print(",\"len\":");    f.print((int)body.length());
  f.print(",\"body\":\"");
  for (size_t i=0;i<body.length();++i) {
    char c = body[i];
    if (c=='\"') f.print("\\\"");
    else if (c=='\\') f.print("\\\\");
    else if ((uint8_t)c < 32) {} else f.print(c);
  }
  f.print("\"}");
  f.close();
}

// OTA-лог в файл (всегда JSON)
static void ota_log(uint32_t received, const char *stage, const char *err = nullptr) {
  File f = LittleFS.open("/ota_log.json", "w");
  if (!f) return;
  f.print("{\"received\":"); f.print(received);
  f.print(",\"stage\":\"");  f.print(stage); f.print("\"");
  if (err) {
    f.print(",\"error\":\"");
    for (const char *p = err; *p; ++p) f.print(*p=='\"' ? "\\\"" : String(*p));
    f.print("\"");
  }
  f.print("}");
  f.close();
}

// ─────────────────────────────────────────────
// Handlers
static esp_err_t index_handler(httpd_req_t *req) {
  httpd_resp_set_type(req, "text/html");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  return httpd_resp_send(req, (const char*)index_html, strlen(index_html));
}

static esp_err_t version_handler(httpd_req_t *req) {
  httpd_resp_set_type(req, "application/json");
  String s = String("{\"version\":\"") + FW_VERSION + "\"}";
  return httpd_resp_send(req, s.c_str(), s.length());
}

static esp_err_t routes_handler(httpd_req_t *req) {
  return send_json(req,
    "{\"routes\":[\"/\",\"/routes\",\"/version\",\"/ytest\",\"/ydlog\",\"/capture\",\"/save\",\"/mkdir\",\"/ydlist\",\"/update\",\"/otalog\",\"/stream(:81)\"]}");
}

static esp_err_t ydlog_handler(httpd_req_t *req) {
  if (!LittleFS.exists("/yd_log.json")) {
    return send_json(req, "{\"stage\":\"idle\",\"note\":\"yd log not created\"}");
  }
  File f = LittleFS.open("/yd_log.json", "r");
  if (!f) {
    return send_json(req, "{\"stage\":\"error\",\"error\":\"open yd_log failed\"}");
  }
  String s = f.readString(); f.close();
  return send_json(req, s);
}

static esp_err_t otalog_handler(httpd_req_t *req) {
  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  if (!LittleFS.exists("/ota_log.json")) {
    const char *stub = "{\"received\":0,\"stage\":\"idle\",\"note\":\"log not created yet\"}";
    return httpd_resp_send(req, stub, strlen(stub));
  }
  File f = LittleFS.open("/ota_log.json", "r");
  if (!f) {
    const char *err = "{\"received\":0,\"stage\":\"error\",\"error\":\"open failed\"}";
    return httpd_resp_send(req, err, strlen(err));
  }
  String s = f.readString(); f.close();
  return httpd_resp_send(req, s.c_str(), s.length());
}

static esp_err_t ytest_handler(httpd_req_t *req) {
  WiFiClientSecure client; client.setInsecure();
  HTTPClient http;
  http.setTimeout(15000);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.begin(client, "https://cloud-api.yandex.net/v1/disk");
  http.addHeader("Authorization", String("OAuth ") + YD_OAUTH_TOKEN);

  int code = http.GET();
  String body = http.getString();
  http.end();

  yd_write_min_log("ytest", code, body);

  String out = String("{\"code\":") + code + ",\"len\":" + body.length() + "}";
  return send_json(req, out);
}

static esp_err_t capture_handler(httpd_req_t *req) {
  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) { httpd_resp_send_500(req); return ESP_FAIL; }
  httpd_resp_set_type(req, "image/jpeg");
  httpd_resp_set_hdr(req, "Content-Disposition", "inline; filename=capture.jpg");
  httpd_resp_send(req, (const char*)fb->buf, fb->len);
  esp_camera_fb_return(fb);
  return ESP_OK;
}

static esp_err_t stream_handler(httpd_req_t *req) {
  static const char* CT = "multipart/x-mixed-replace;boundary=frame";
  static const char* B  = "\r\n--frame\r\n";
  static const char* P  = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";
  char part[64];

  httpd_resp_set_type(req, CT);

  while (true) {
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) { httpd_resp_send_500(req); return ESP_FAIL; }
    if (httpd_resp_send_chunk(req, B, strlen(B)) != ESP_OK) { esp_camera_fb_return(fb); break; }
    snprintf(part, sizeof(part), P, fb->len);
    if (httpd_resp_send_chunk(req, part, strlen(part)) != ESP_OK) { esp_camera_fb_return(fb); break; }
    if (httpd_resp_send_chunk(req, (const char*)fb->buf, fb->len) != ESP_OK) { esp_camera_fb_return(fb); break; }
    esp_camera_fb_return(fb);
    delay(1);
  }
  return ESP_OK;
}

// Сохранение кадра → загрузка на Диск → удаление локального файла при успехе (с автопереименованием)
static esp_err_t save_handler(httpd_req_t *req) {
  // имя файла из query (?name=foo)
  String filename = "capture.jpg";
  char q[96];
  if (httpd_req_get_url_query_str(req, q, sizeof(q)) == ESP_OK) {
    char val[64];
    if (httpd_query_key_value(q, "name", val, sizeof(val)) == ESP_OK) {
      String n = val; n.trim();
      if (n.length()) filename = (n.endsWith(".jpg")||n.endsWith(".jpeg")) ? n : (n + ".jpg");
    }
  }

  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) { httpd_resp_send_500(req); return ESP_FAIL; }

  String localPath = "/" + filename;
  File f = LittleFS.open(localPath, "w");
  if (!f) { esp_camera_fb_return(fb); httpd_resp_send_500(req); return ESP_FAIL; }
  f.write(fb->buf, fb->len);
  f.close();
  size_t sz = fb->len;
  esp_camera_fb_return(fb);

  String remoteFinal;
  bool ok = ydEnsureUploadNoOverwrite(String(YD_BASE_DIR), localPath, filename, remoteFinal);
  if (ok) LittleFS.remove(localPath);

  String out = String("{\"ok\":") + (ok?"true":"false") +
               ",\"bytes\":" + sz + ",\"remote\":\"" + remoteFinal + "\"}";
  return send_json(req, out);
}

// Создание папки (POST body = plain text с путём)
static esp_err_t mkdir_handler(httpd_req_t *req) {
  char buf[192];
  int r = httpd_req_recv(req, buf, sizeof(buf)-1);
  if (r <= 0) { httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Bad Request"); return ESP_FAIL; }
  buf[r] = 0;
  String dir = String(buf); dir.trim();
  bool ok = ydCreateFolder(dir);
  return send_json(req, String("{\"ok\":") + (ok?"true":"false") + ",\"dir\":\"" + dir + "\"}");
}

// Список файлов Я.Диска: GET /ydlist?dir=/Esp32Cam
static esp_err_t ydlist_handler(httpd_req_t *req) {
  char q[160];
  String dir = YD_BASE_DIR;
  if (httpd_req_get_url_query_str(req, q, sizeof(q)) == ESP_OK) {
    char val[128];
    if (httpd_query_key_value(q, "dir", val, sizeof(val)) == ESP_OK) dir = val;
  }
  String out;
  if (!ydListFolder(dir, out)) return send_json(req, "{\"ok\":false,\"error\":\"list failed\"}");
  return send_json(req, out);
}

// OTA: POST /update (Content-Type: application/octet-stream)
static esp_err_t update_handler(httpd_req_t *req) {
  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

  // первая запись в лог — чтобы файл точно появился
  ota_log(0, "begin", nullptr);

  // остановить стрим и выгрузить камеру (снимаем нагрузку с PSRAM/PLL)
  if (g_stream_server) { httpd_stop(g_stream_server); g_stream_server = nullptr; }
  esp_camera_deinit();

  const int total_len = req->content_len;
  if (total_len <= 0) {
    ota_log(0, "begin", "empty");
    return send_json(req, "{\"ok\":false,\"err\":\"empty upload\"}");
  }

  if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
    String err = String("begin: ") + Update.errorString();
    ota_log(0, "begin", err.c_str());
    return send_json(req, String("{\"ok\":false,\"err\":\"") + err + "\"}");
  }

  const size_t CHUNK = 512;   // маленькие блоки устойчивее
  uint8_t *buf = (uint8_t*)malloc(CHUNK);
  if (!buf) { Update.abort(); ota_log(0,"alloc","no mem"); return send_json(req, "{\"ok\":false,\"err\":\"no mem\"}"); }

  int remaining = total_len;
  size_t received = 0;
  int retry = 200;            // до ~20с суммарных ретраев
  ota_log(0, "recv");

  while (remaining > 0) {
    int toRead = remaining > (int)CHUNK ? (int)CHUNK : remaining;
    int r = httpd_req_recv(req, (char*)buf, toRead);
    if (r < 0) {  // таймаут чтения — пробуем ещё
      if (--retry <= 0) { free(buf); Update.abort(); ota_log(received,"recv","timeout"); return send_json(req, "{\"ok\":false,\"stage\":\"recv\",\"err\":\"timeout\"}"); }
      delay(100); yield();
      continue;
    }
    if (r == 0) { free(buf); Update.abort(); ota_log(received,"recv","client closed"); return send_json(req, "{\"ok\":false,\"stage\":\"recv\",\"err\":\"client closed\"}"); }

    if (Update.write(buf, r) != (size_t)r) {
      String err = String("write: ") + Update.errorString();
      free(buf); Update.abort(); ota_log(received,"write",err.c_str());
      return send_json(req, String("{\"ok\":false,\"stage\":\"write\",\"err\":\"") + err + "\"}");
    }

    received  += r;
    remaining -= r;
    retry      = 200;
    if ((received % 8192) < CHUNK) ota_log(received, "recv");
    yield();
  }

  free(buf);

  if (!Update.end(true)) {
    String err = String("end: ") + Update.errorString();
    ota_log(received, "end", err.c_str());
    return send_json(req, String("{\"ok\":false,\"stage\":\"end\",\"err\":\"") + err + "\"}");
  }

  ota_log(received, "done");
  send_json(req, String("{\"ok\":true,\"bytes\":") + received + "}");
  delay(600);
  esp_restart();
  return ESP_OK;
}

// ─────────────────────────────────────────────
// Старт HTTP серверов
void startCameraServer() {
  // :80 UI/API
  httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
  cfg.stack_size = 12288;
  cfg.recv_wait_timeout = 120;   // побольше таймауты
  cfg.send_wait_timeout = 120;
  cfg.lru_purge_enable = true;
  cfg.max_uri_handlers = 16;

  if (httpd_start(&g_http_server, &cfg) == ESP_OK) {
    static httpd_uri_t uri_index   = { "/",        HTTP_GET,  index_handler,   NULL };
    static httpd_uri_t uri_version = { "/version", HTTP_GET,  version_handler, NULL };
    static httpd_uri_t uri_routes  = { "/routes",  HTTP_GET,  routes_handler,  NULL };
    static httpd_uri_t uri_ytest   = { "/ytest",   HTTP_GET,  ytest_handler,   NULL };
    static httpd_uri_t uri_ydlog   = { "/ydlog",   HTTP_GET,  ydlog_handler,   NULL };
    static httpd_uri_t uri_capture = { "/capture", HTTP_GET,  capture_handler, NULL };
    static httpd_uri_t uri_save    = { "/save",    HTTP_GET,  save_handler,    NULL };
    static httpd_uri_t uri_mkdir   = { "/mkdir",   HTTP_POST, mkdir_handler,   NULL };
    static httpd_uri_t uri_ydlist  = { "/ydlist",  HTTP_GET,  ydlist_handler,  NULL };
    static httpd_uri_t uri_update  = { "/update",  HTTP_POST, update_handler,  NULL };
    static httpd_uri_t uri_otalog  = { "/otalog",  HTTP_GET,  otalog_handler,  NULL };

    httpd_register_uri_handler(g_http_server, &uri_index);
    httpd_register_uri_handler(g_http_server, &uri_version);
    httpd_register_uri_handler(g_http_server, &uri_routes);
    httpd_register_uri_handler(g_http_server, &uri_ytest);
    httpd_register_uri_handler(g_http_server, &uri_ydlog);
    httpd_register_uri_handler(g_http_server, &uri_capture);
    httpd_register_uri_handler(g_http_server, &uri_save);
    httpd_register_uri_handler(g_http_server, &uri_mkdir);
    httpd_register_uri_handler(g_http_server, &uri_ydlist);
    httpd_register_uri_handler(g_http_server, &uri_update);
    httpd_register_uri_handler(g_http_server, &uri_otalog);

    // создать пустой лог OTA, чтобы /otalog всегда отвечал JSON
    if (!LittleFS.exists("/ota_log.json")) {
      File f = LittleFS.open("/ota_log.json", "w");
      if (f) { f.print("{\"received\":0,\"stage\":\"idle\"}"); f.close(); }
    }
  }

  // :81 stream
  httpd_config_t cfgs = HTTPD_DEFAULT_CONFIG();
  cfgs.stack_size = 12288;
  cfgs.server_port = 81;
  cfgs.ctrl_port   = 32769;
  cfgs.recv_wait_timeout = 60;
  cfgs.send_wait_timeout = 60;
  cfgs.lru_purge_enable = true;
  cfgs.max_uri_handlers = 4;

  if (httpd_start(&g_stream_server, &cfgs) == ESP_OK) {
    static httpd_uri_t stream_uri = { "/stream", HTTP_GET, stream_handler, NULL };
    httpd_register_uri_handler(g_stream_server, &stream_uri);
  }
}
