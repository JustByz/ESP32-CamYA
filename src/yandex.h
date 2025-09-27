#pragma once
#include <Arduino.h>

// Было
bool ydCreateFolder(const String &dir);
bool ydEnsureUpload(const String &remoteDir, const String &localPath, const String &remoteFile);

// Новое
bool ydResourceExists(const String &path); // disk:/... или /...
// Загрузка без перезаписи: подбирает уникальное имя (name.jpg → name(1).jpg → …)
bool ydEnsureUploadNoOverwrite(const String &remoteDir,
                               const String &localPath,
                               const String &baseFileName,
                               String &outRemotePath);

// Получить список файлов в папке (JSON-строка)
bool ydListFolder(const String &dir, String &outJson);