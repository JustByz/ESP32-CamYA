#pragma once
#include <Arduino.h>

// ⚙️ Конфиг задаётся в Config.h
#include "Config.h"

// Создание папки
bool ydCreateFolder(const String &path);

// Проверка существования ресурса
bool ydResourceExists(const String &path);

// Получение ссылки для загрузки
String ydGetUploadHref(const String &remotePath, bool overwrite = true);

// Загрузка файла на Яндекс.Диск
bool ydUploadFile(const String &localPath, const String &remotePath);

// Удобная обёртка: сначала создать папку (если нет), потом загрузить файл
bool ydEnsureUpload(const String &folder, const String &localFile, const String &remoteFile);
