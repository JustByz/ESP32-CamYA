#pragma once
#include <Arduino.h>

bool   ydUploadFile(const String &localPath, const String &remotePath);
String ydUploadFileWithRename(const String &localPath, const String &remotePath);
bool   ydEnsureDirsREST(const String &dir);

// Вспомогательное URL-кодирование (для путей с пробелами/кириллицей)
String ydUrlEncode(const String &s);
