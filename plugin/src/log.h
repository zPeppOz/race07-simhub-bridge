#pragma once

void LogOpen(const char* path);
void Log(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
void LogClose();
