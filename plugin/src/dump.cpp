// Record format (little endian):
//   u32 'R7DS'  u32 tickMs  f64 sessionTime  u32 regionCount
//   regionCount x { u32 address  u32 length  u8[length] }
// Every snapshot carries the whole .data section of the game executable,
// which holds the static vehicle array and all scoring/session globals.
// The game thread only copies memory; a worker thread writes the file.
#include "dump.h"

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "log.h"

namespace {

FILE* g_file = nullptr;
DWORD g_intervalMs = 0;
DWORD g_last = 0;
BYTE* g_dataStart = nullptr;
DWORD g_dataSize = 0;

BYTE* g_buffer = nullptr;  // copy of .data waiting to be written
DWORD g_bufferTick = 0;
double g_bufferTime = 0;
volatile LONG g_bufferBusy = 0;  // 1 while the worker owns g_buffer
HANDLE g_wake = nullptr;
HANDLE g_thread = nullptr;
volatile bool g_stop = false;

void FindDataSection() {
  auto* base = reinterpret_cast<BYTE*>(GetModuleHandleA(nullptr));
  auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + reinterpret_cast<IMAGE_DOS_HEADER*>(base)->e_lfanew);
  auto* sec = IMAGE_FIRST_SECTION(nt);
  for (int i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++sec) {
    if (memcmp(sec->Name, ".data", 6) == 0) {
      g_dataStart = base + sec->VirtualAddress;
      g_dataSize = sec->Misc.VirtualSize;
    }
  }
}

DWORD WINAPI Writer(LPVOID) {
  while (WaitForSingleObject(g_wake, INFINITE) == WAIT_OBJECT_0 && !g_stop) {
    const DWORD magic = 0x53443752;  // "R7DS"
    const DWORD regions = 1;
    const DWORD addr = static_cast<DWORD>(reinterpret_cast<UINT_PTR>(g_dataStart));
    fwrite(&magic, 4, 1, g_file);
    fwrite(&g_bufferTick, 4, 1, g_file);
    fwrite(&g_bufferTime, 8, 1, g_file);
    fwrite(&regions, 4, 1, g_file);
    fwrite(&addr, 4, 1, g_file);
    fwrite(&g_dataSize, 4, 1, g_file);
    fwrite(g_buffer, 1, g_dataSize, g_file);
    fflush(g_file);
    InterlockedExchange(&g_bufferBusy, 0);
  }
  return 0;
}

}  // namespace

void DumpOpen(const char* path, float interval) {
  if (interval <= 0) return;
  FindDataSection();
  if (!g_dataStart) {
    Log("dump: .data section not found");
    return;
  }
  g_buffer = static_cast<BYTE*>(malloc(g_dataSize));
  g_file = fopen(path, "ab");
  if (!g_buffer || !g_file) {
    Log("dump: cannot open %s", path);
    return;
  }
  g_intervalMs = static_cast<DWORD>(interval * 1000);
  g_wake = CreateEventA(nullptr, FALSE, FALSE, nullptr);
  g_thread = CreateThread(nullptr, 0, Writer, nullptr, 0, nullptr);
  Log("dump: writing %s every %lu ms (.data %p + %lu bytes)", path, g_intervalMs, g_dataStart, g_dataSize);
}

void DumpTick(double sessionTime) {
  if (!g_thread) return;
  const DWORD now = GetTickCount();
  if (g_last && now - g_last < g_intervalMs) return;
  if (InterlockedCompareExchange(&g_bufferBusy, 1, 0) != 0) return;  // previous write still running
  g_last = now;
  memcpy(g_buffer, g_dataStart, g_dataSize);
  g_bufferTick = now;
  g_bufferTime = sessionTime;
  SetEvent(g_wake);
}

void DumpClose() {
  if (g_thread) {
    g_stop = true;
    SetEvent(g_wake);
    WaitForSingleObject(g_thread, 2000);
  }
  if (g_file) fclose(g_file);
  g_file = nullptr;
}
