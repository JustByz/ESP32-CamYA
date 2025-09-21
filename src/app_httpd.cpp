#include "app_httpd.h"
#include "esp_http_server.h"
#include "esp_camera.h"
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <FS.h>
#include <LittleFS.h>

#include "yandex.h"
#include "Config.h"
#include "camera_index.h"

// ───────────────── helpers ─────────────────
static inline esp_err_t send_json(httpd_req_t *req, const String &s) {
  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  return httpd_resp_send(req, s.c_str(), s.length());
}

static inline void fs_write_log_min(const char *stage, int code, const String &body) {
  File f = LittleFS.open("/yd_log.json", "w");
  if (!f) return;
  f.print("{\"stage\":\"");
  // escape stage
  for (const char *p = stage; *p; ++p) { if (*p=='\"') f.print("\\\""); else f.print(*p); }
  f.print("\",\"code\":");
  f.print(code);
  f.print(",\"len\":");
  f.print(body.length());
  f.print(",\"body\":\"");
  for (size_t i=0;i<body.length();++i) {
    char c = body[i];
    if (c=='\"') f.print("\\\"");
    else if (c=='\\') f.print("\\\\");
    else if ((uint8_t)c < 32) {/*skip control*/}
    else f.print(c);
  }
  f.print("\"}");
  f.close();
}

// ───────────────── handlers ─────────────────
static esp_err_t index_handler(httpd_req_t *req) {
  httpd_resp_set_type(req, "text/html");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  return httpd_resp_send(req, (const char *)index_html, strlen(index_html));
}

static esp_err_t routes_handler(httpd_req_t *req) {
  return send_json(req, "{\"routes\":[\"/\",\"/routes\",\"/ytest\",\"/ydlog\",\"/capture\",\"/save\",\"/mkdir\",\"/stream(:81)\"]}");
}

static esp_err_t ydlog_handler(httpd_req_t *req) {
  if (!LittleFS.exists("/yd_log.json")) { httpd_resp_send_404(req); return ESP_FAIL; }
  File f = LittleFS.open("/yd_log.json", "r");
  if (!f) { httpd_resp_send_500(req); return ESP_FAIL; }
  String content = f.readString(); f.close();
  return send_json(req, content);
}

// Тест доступа к API Диска (минимум памяти, TLS клиент на стеке)
static esp_err_t ytest_handler(httpd_req_t *req) {
  WiFiClientSecure client; client.setInsecure();
  HTTPClient http;
  http.setTimeout(10000);
  http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
  http.begin(client, "https://cloud-api.yandex.net/v1/disk");
  http.addHeader("Authorization", String("OAuth ") + YD_OAUTH_TOKEN);

  const int code = http.GET();
  const String body = http.getString();
  http.end();

  // минимальный лог
  fs_write_log_min("ytest", code, body);

  String out; out.reserve(48);
  out += "{\"code\":";
  out += String(code);
  out += ",\"len\":";
  out += String(body.length());
  out += "}";
  return send_json(req, out);
}

// Сделать JPEG-снимок и отдать браузеру
static esp_err_t capture_handler(httpd_req_t *req) {
  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) { httpd_resp_send_500(req); return ESP_FAIL; }
  httpd_resp_set_type(req, "image/jpeg");
  httpd_resp_set_hdr(req, "Content-Disposition", "inline; filename=capture.jpg");
  httpd_resp_send(req, (const char *)fb->buf, fb->len);
  esp_camera_fb_return(fb);
  return ESP_OK;
}

// MJPEG-поток (порт 81), корректный выход при разрыве
static esp_err_t stream_handler(httpd_req_t *req) {
  static const char* CT = "multipart/x-mixed-replace;boundary=frame";
  static const char* B  = "\r\n--frame\r\n";
  static const char* P  = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";
  char part_buf[64];

  httpd_resp_set_type(req, CT);

  while (true) {
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) { httpd_resp_send_500(req); return ESP_FAIL; }
    if (fb->format != PIXFORMAT_JPEG) { esp_camera_fb_return(fb); httpd_resp_send_500(req); return ESP_FAIL; }
    if (httpd_resp_send_chunk(req, B, strlen(B)) != ESP_OK) { esp_camera_fb_return(fb); break; }
    snprintf(part_buf, sizeof(part_buf), P, fb->len);
    if (httpd_resp_send_chunk(req, part_buf, strlen(part_buf)) != ESP_OK) { esp_camera_fb_return(fb); break; }
    if (httpd_resp_send_chunk(req, (const char *)fb->buf, fb->len) != ESP_OK) { esp_camera_fb_return(fb); break; }
    esp_camera_fb_return(fb);
  }
  return ESP_OK;
}

// Сохранить фото локально → загрузить на Диск → при успехе удалить локально
static esp_err_t save_handler(httpd_req_t *req) {
  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) { httpd_resp_send_500(req); return ESP_FAIL; }

  const String localPath = "/capture.jpg";
  File f = LittleFS.open(localPath, "w");
  if (!f) { esp_camera_fb_return(fb); httpd_resp_send_500(req); return ESP_FAIL; }
  f.write(fb->buf, fb->len);
  f.close();
  size_t sz = fb->len;
  esp_camera_fb_return(fb);

  const String folder = String(YD_BASE_DIR);
  const String remoteFile = "capture.jpg";
  const bool ok = ydEnsureUpload(folder, localPath, remoteFile);
  if (ok) LittleFS.remove(localPath);

  String out; out.reserve(64);
  out += "{\"ok\":";
  out += (ok ? "true" : "false");
  out += ",\"bytes\":";
  out += String(sz);
  out += "}";
  return send_json(req, out);
}

// Создать папку: принимает имя в теле POST (plain text)
static esp_err_t mkdir_handler(httpd_req_t *req) {
  char buf[128];
  const int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
  if (ret <= 0) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Bad Request");
    return ESP_FAIL;
  }
  buf[ret] = '\0';
  const String folder = String(buf);

  const bool ok = ydCreateFolder(folder);
  String out = String("{\"ok\":") + (ok ? "true" : "false") + "}";
  return send_json(req, out);
}

// ───────────────── start servers ─────────────────
void startCameraServer() {
  // HTTP :80 — увеличим стек для TLS/логики
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.stack_size = 12288;            // ⬅️ главное: больше стека
  config.recv_wait_timeout = 30;
  config.send_wait_timeout = 30;

  httpd_handle_t server = NULL;
  if (httpd_start(&server, &config) == ESP_OK) {
    httpd_uri_t uri_index   = { .uri="/",       .method=HTTP_GET, .handler=index_handler,  .user_ctx=NULL };
    httpd_uri_t uri_routes  = { .uri="/routes", .method=HTTP_GET, .handler=routes_handler, .user_ctx=NULL };
    httpd_uri_t uri_ytest   = { .uri="/ytest",  .method=HTTP_GET, .handler=ytest_handler,  .user_ctx=NULL };
    httpd_uri_t uri_ydlog   = { .uri="/ydlog",  .method=HTTP_GET, .handler=ydlog_handler,  .user_ctx=NULL };
    httpd_uri_t uri_capture = { .uri="/capture",.method=HTTP_GET, .handler=capture_handler,.user_ctx=NULL };
    httpd_uri_t uri_save    = { .uri="/save",   .method=HTTP_GET, .handler=save_handler,   .user_ctx=NULL };
    httpd_uri_t uri_mkdir   = { .uri="/mkdir",  .method=HTTP_POST,.handler=mkdir_handler,  .user_ctx=NULL };

    httpd_register_uri_handler(server, &uri_index);
    httpd_register_uri_handler(server, &uri_routes);
    httpd_register_uri_handler(server, &uri_ytest);
    httpd_register_uri_handler(server, &uri_ydlog);
    httpd_register_uri_handler(server, &uri_capture);
    httpd_register_uri_handler(server, &uri_save);
    httpd_register_uri_handler(server, &uri_mkdir);
  }

  // Stream :81 — тоже увеличим стек
  httpd_config_t config_stream = HTTPD_DEFAULT_CONFIG();
  config_stream.stack_size = 12288;     // ⬅️ больше стека
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
