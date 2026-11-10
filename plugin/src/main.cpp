// Race 07 "SBDT" plugin entry point.
//
// The game LoadLibrary()s every plugins\*.sbp at startup (right before it
// creates its window), calls the export GetSBDTDLL() and keeps the returned
// object. Each frame it ORs GetFlags() of all plugins and invokes the
// matching virtual callbacks (__thiscall, callee cleans the stack):
//   vtbl+0x04 flags&0x01  per-frame session tick (main loop)
//   vtbl+0x08 flags&0x02  player physics: position/orientation/velocity
//   vtbl+0x0c flags&0x08  player physics: extended vehicle state
//   vtbl+0x10 flags&0x10  two pointer arguments (camera/listener)
//   vtbl+0x14 flags&0x04  input state
//   vtbl+0x18 GetFlags()
// We only subscribe to the per-frame tick and read the game state directly,
// publishing it as the rFactor 1 shared memory SimHub's GTR2 reader opens.
#include <windows.h>
#include <string.h>

#include "borderless.h"
#include "config.h"
#include "dump.h"
#include "log.h"
#include "race07.h"
#include "rf1_shared.h"

namespace {

char g_dir[MAX_PATH];
Config g_cfg;
bool g_dataReady = false;
HANDLE g_mapping = nullptr;
rf1::Shared* g_view = nullptr;

bool OpenSharedMemory() {
  g_mapping = CreateFileMappingA(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(rf1::Shared),
                                 rf1::kSharedName);
  if (!g_mapping) return false;
  g_view = static_cast<rf1::Shared*>(MapViewOfFile(g_mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(rf1::Shared)));
  if (!g_view) return false;
  memset(g_view, 0, sizeof(rf1::Shared));
  return true;
}

void CloseSharedMemory() {
  if (g_view) UnmapViewOfFile(g_view);
  if (g_mapping) CloseHandle(g_mapping);
  g_view = nullptr;
  g_mapping = nullptr;
}

class Plugin {
 public:
  virtual void __thiscall Slot0() {}
  virtual void __thiscall OnFrame(void*) { Tick(); }
  virtual void __thiscall OnPhysics(void*) {}
  virtual void __thiscall OnVehicle(void*) {}
  virtual void __thiscall OnCamera(void*, void*) {}
  virtual void __thiscall OnInput(void*) {}
  virtual unsigned __thiscall GetFlags() { return g_dataReady ? 0x01u : 0u; }

 private:
  static void Tick() {
    // Built aside and copied in one go to keep torn reads short.
    static rf1::Shared frame;
    if (!race07::Read(frame)) return;
    if (g_view) memcpy(g_view, &frame, sizeof(frame));
    DumpTick(frame.currentET);
  }
};

Plugin g_plugin;

void PluginPath(const char* file, char* out) {
  strcpy(out, g_dir);
  strcat(out, file);
}

void Init(HMODULE self) {
  GetModuleFileNameA(self, g_dir, MAX_PATH);
  if (char* slash = strrchr(g_dir, '\\')) slash[1] = '\0';
  char path[MAX_PATH];
  PluginPath("race07bridge.log", path);
  LogOpen(path);
  PluginPath("race07bridge.ini", path);
  g_cfg = LoadConfig(path);
  Log("race07bridge loaded from %s", g_dir);

  InstallWindowHooks(g_cfg.window);

  if (!race07::Init()) {
    Log("unsupported game executable: SimHub data disabled");
    return;
  }
  if (g_cfg.sharedMemory) {
    if (OpenSharedMemory())
      Log("shared memory: %s created", rf1::kSharedName);
    else
      Log("shared memory: could not create %s (error %lu)", rf1::kSharedName, GetLastError());
  }
  PluginPath("race07bridge.dump", path);
  DumpOpen(path, g_cfg.dumpInterval);
  g_dataReady = true;
}

}  // namespace

extern "C" __declspec(dllexport) void* GetSBDTDLL() { return &g_plugin; }

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID) {
  if (reason == DLL_PROCESS_ATTACH) {
    DisableThreadLibraryCalls(inst);
    Init(inst);
  } else if (reason == DLL_PROCESS_DETACH) {
    CloseSharedMemory();
    DumpClose();
    LogClose();
  }
  return TRUE;
}
