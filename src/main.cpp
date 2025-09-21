#include <Arduino.h>
#include <WiFi.h>
#include <FS.h>           // 👈 обязательно
#include <LittleFS.h>     // 👈 обязательно
#include "esp_camera.h"
#include "Config.h"
#include "app_httpd.h"

// ВАЖНО: выбрать модель камеры ДО подключения camera_pins.h
#define CAMERA_MODEL_AI_THINKER  // если у тебя именно AI Thinker ESP32-CAM
#include "camera_pins.h"

#ifndef WIFI_SSID
#define WIFI_SSID "YourSSID"
#define WIFI_PASS "YourPASS"
#endif

void setup() {
  Serial.begin(115200);
  Serial.setDebugOutput(true);
  Serial.println();

  if (!LittleFS.begin(true)) {
    Serial.println("LittleFS mount failed");
  } else {
    Serial.println("LittleFS mounted OK");
  }

  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("Connecting to Wi-Fi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWi-Fi connected");
  Serial.print("IP: ");
  Serial.println(WiFi.localIP());

  // Конфигурация камеры
  camera_config_t config = {};
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer   = LEDC_TIMER_0;
  config.pin_d0       = Y2_GPIO_NUM;
  config.pin_d1       = Y3_GPIO_NUM;
  config.pin_d2       = Y4_GPIO_NUM;
  config.pin_d3       = Y5_GPIO_NUM;
  config.pin_d4       = Y6_GPIO_NUM;
  config.pin_d5       = Y7_GPIO_NUM;
  config.pin_d6       = Y8_GPIO_NUM;
  config.pin_d7       = Y9_GPIO_NUM;
  config.pin_xclk     = XCLK_GPIO_NUM;
  config.pin_pclk     = PCLK_GPIO_NUM;
  config.pin_vsync    = VSYNC_GPIO_NUM;
  config.pin_href     = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM;   // новое имя поля в камере
  config.pin_sccb_scl = SIOC_GPIO_NUM;   // новое имя поля в камере
  config.pin_pwdn     = PWDN_GPIO_NUM;
  config.pin_reset    = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;

  if (psramFound()) {
    config.frame_size   = FRAMESIZE_VGA;
    config.jpeg_quality = 10;
    config.fb_count     = 2;
  } else {
    config.frame_size   = FRAMESIZE_QVGA;
    config.jpeg_quality = 12;
    config.fb_count     = 1;
  }

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x\n", err);
    return;
  }

  startCameraServer();
}

void loop() {
  delay(10000);
}
