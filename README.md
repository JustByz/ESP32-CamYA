# 📷 ESP32-CAM Web + Яндекс.Диск

Устройство на **ESP32-CAM**, позволяющее делать снимки и потоковое видео, управлять через веб-интерфейс, сохранять фото во встроенную файловую систему LittleFS и автоматически отправлять их в Яндекс.Диск для хранения или создания датасетов.  
Теперь поддерживается **OTA-обновление прошивки** прямо из веб-интерфейса.

---

## 📑 Оглавление
- [📷 ESP32-CAM Web + Яндекс.Диск](#-esp32-cam-web--яндексдиск)
  - [📑 Оглавление](#-оглавление)
  - [🚀 Особенности](#-особенности)
  - [🏗 Архитектура проекта](#-архитектура-проекта)
  - [📂 Структура исходников](#-структура-исходников)
  - [🛠 Поддерживаемое железо](#-поддерживаемое-железо)
  - [📌 Подключение ESP32-CAM](#-подключение-esp32-cam)
  - [📦 Установка](#-установка)
    - [Веб-интерфейс](#веб-интерфейс)

---

## 🚀 Особенности

- 📸 Получение снимков (JPEG)  
- 🎥 Онлайн-поток MJPEG (порт 81)  
- 💾 Сохранение фото в LittleFS  
- 🌐 Управление через веб-интерфейс (на русском) и Serial  
- ☁️ Автоматическая загрузка на Яндекс.Диск (REST API)  
- 🗑 Удаление файлов после успешной загрузки  
- 📂 Просмотр файлов в папке Яндекс.Диска  
- 🔄 OTA-обновление прошивки прямо из веб  

---

## 🏗 Архитектура проекта

- **ESP32-CAM (AI Thinker)** управляет камерой и сервером.  
- Веб-сервер (порт 80) обслуживает интерфейс и API.  
- MJPEG-поток транслируется через порт 81.  
- Файлы сохраняются в LittleFS, затем отправляются на Яндекс.Диск.  
- OTA обновление выполняется через `/update`, лог процесса доступен по `/otalog`.  

---

## 📂 Структура исходников

```text
src/
 ├── main.cpp        // Инициализация камеры, Wi-Fi, запуск веб- и стрим-сервера
 ├── app_httpd.cpp   // Веб-сервер: обработка роутов (/capture, /save, /mkdir, /update и др.)
 ├── yandex.cpp/.h   // Работа с REST API Яндекс.Диска (создание папок, загрузка файлов, логирование)
 ├── camera_index.h  // HTML+JS веб-интерфейса (вшитая страница управления)
 ├── camera_pins.h   // Конфигурация пинов модуля камеры (AI Thinker ESP32-CAM)
 └── Config.h        // Общие параметры (порты, настройки FS, константы)

include/
 └── secret.h        // 🔒 Wi-Fi креды и OAuth-токен Яндекс.Диска (не коммитится, есть в .gitignore)

docs/
 └── screenshot.png  // Скриншот веб-интерфейса ESP32-CAM
```

---

## 🛠 Поддерживаемое железо

- **ESP32-CAM AI Thinker**  
- Плата-переходник с USB или UART-адаптер  
- Источник питания ≥ 5В 1А (⚠️ USB часто не хватает!)  

---

## 📌 Подключение ESP32-CAM

| Пин | Назначение |
|-----|------------|
| 5V  | Питание |
| GND | Земля |
| IO0 → GND | Режим прошивки (BOOT) |

---

## 📦 Установка

```bash
git clone https://github.com/JustByz/ESP32-CamYA.git
cd ESP32-CamYA
```

Создаём `include/secret.h`:

```cpp
#pragma once
#define WIFI_SSID "YourWiFiSSID"
#define WIFI_PASS "YourWiFiPassword"

#define YD_OAUTH_TOKEN "ya0.AQAAA...your_token..."
#define YD_BASE_DIR "/Esp32Cam"
```

Сборка и прошивка (PlatformIO):

```bash
pio run -t upload
pio run -t uploadfs
pio device monitor
```

---

## 🎛 Использование

### Команды через Serial

- `save` → сделать снимок и отправить на Яндекс.Диск  
- `stream on` → включить поток  
- `stream off` → выключить поток  
- `mkdir test` → создать папку test в Яндекс.Диске  

### Веб-интерфейс

После подключения к Wi-Fi можно открыть интерфейс:  

![Веб-интерфейс ESP32-CAM](docs/screenshot.png)

- `http://<IP_ESP32>/` → главное окно (стрим + кнопки)  
- `http://<IP_ESP32>/capture` → одиночное фото  
- `http://<IP_ESP32>/save` → фото + загрузка на Диск  
- `http://<IP_ESP32>/mkdir` → POST-запрос → создать папку  
- `http://<IP_ESP32>/ydlist?name=...` → список файлов в папке  
- `http://<IP_ESP32>/ydlog` → просмотр логов работы с API  
- `http://<IP_ESP32>/update` → OTA обновление (загрузка .bin)  
- `http://<IP_ESP32>/otalog` → лог OTA процесса  
- `http://<IP_ESP32>/version` → версия прошивки  

---

## 🌐 API эндпоинты

- `/` → главная страница (UI), GET → text/html  
- `/routes` → список маршрутов, GET → application/json  
- `/ytest` → тест подключения к Я.Диску, GET → application/json  
- `/ydlog` → последний лог Я.Диска, GET → application/json  
- `/capture` → одиночный снимок, GET → image/jpeg  
- `/save` → снимок + загрузка в Я.Диск, GET → application/json  
- `/mkdir` → создание папки, POST → application/json  
- `/ydlist` → список файлов в папке, GET → application/json  
- `/update` → OTA обновление, POST → загрузка .bin  
- `/otalog` → лог OTA, GET → application/json  
- `/version` → версия прошивки, GET → application/json  
- `/stream` → MJPEG поток, GET → multipart/x-mixed-replace (порт 81)  

---

## ☁️ Как работает загрузка в Яндекс.Диск

1. ESP делает снимок и сохраняет в LittleFS  
2. Вызывается `ydEnsureUpload()` → проверка/создание папки  
3. `ydGetUploadHref()` → запрос ссылки для загрузки  
4. `PUT file.jpg → href`  
5. При успехе → локальный файл удаляется  

---

## 🗑 Очистка файловой системы

Команда:

```bash
wipefs
```

Удаляет все данные из LittleFS.

---

## 🔧 Внутренние функции (yandex.cpp)

- `bool ydResourceExists(path)` → проверка существования ресурса  
- `bool ydCreateFolder(path)` → создать папку  
- `String ydGetUploadHref(remotePath, overwrite)` → получить href  
- `bool ydUploadFile(localPath, remotePath)` → загрузить файл  
- `bool ydEnsureUpload(folder, localFile, remoteFile)` → проверить/создать папку и загрузить  
- `bool ydListFolder(dir, outJson)` → получить список файлов в папке  

---

## 🧪 Примеры последовательностей

Сохранить снимок и отправить в базовую папку:  
```bash
curl http://<IP>/save
curl http://<IP>/ydlog
```

Создать папку и загрузить в неё:  
```bash
curl -X POST http://<IP>/mkdir -d "/Esp32Cam/session1"
curl http://<IP>/save
```

Посмотреть список файлов в папке:  
```bash
curl "http://<IP>/ydlist?name=/Esp32Cam/session1"
```

---

## 🩺 Типичные ошибки

- **403 Forbidden** → токен без прав записи → нужен токен с `disk:read disk:write`  
- **401 Unauthorized** → токен неверен/просрочен → получить новый  
- **500 /capture** → камера не вернула кадр → проверить питание и `#define CAMERA_MODEL_AI_THINKER`  
- **/ydlog → 404** → лога ещё нет → вызвать `/ytest` или `/save`  
- **Ребуты при /ytest или /stream** → исправлено (увеличен стек задач)  

---

## 🔐 Безопасность

- OAuth-токен хранится в `include/secret.h` (не коммитить в git).  
- Для продакшн-использования → ограничить доступ к UI (пароль, VPN, локальная сеть).  

---

## ⚙️ Где править поведение

- Базовая папка на Диске → `YD_BASE_DIR` в `secret.h`  
- Лог → путь `/yd_log.json`  
- OTA лог → `/ota_log.json`  
- Порт стрима → `81` в `startCameraServer()`  

---

## 🔮 Возможные улучшения

- Поддержка Google Drive / Dropbox  
- Автоматическое именование файлов по времени  
- OTA-обновления через веб (уже реализовано ✅)  
- Регулировка параметров камеры через UI  
