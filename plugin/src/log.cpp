#include "log.h"

#include <windows.h>
#include <stdarg.h>
#include <stdio.h>

static FILE* g_log = nullptr;
static CRITICAL_SECTION g_lock;

void LogOpen(const char* path) {
  InitializeCriticalSection(&g_lock);
  g_log = fopen(path, "w");
}

void Log(const char* fmt, ...) {
  if (!g_log) return;
  EnterCriticalSection(&g_lock);
  SYSTEMTIME t;
  GetLocalTime(&t);
  fprintf(g_log, "%02d:%02d:%02d.%03d ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
  va_list ap;
  va_start(ap, fmt);
  vfprintf(g_log, fmt, ap);
  va_end(ap);
  fputc('\n', g_log);
  fflush(g_log);
  LeaveCriticalSection(&g_lock);
}

void LogClose() {
  if (!g_log) return;
  fclose(g_log);
  g_log = nullptr;
  DeleteCriticalSection(&g_lock);
}
