// Borderless windowed mode.
//
// With WINDOWEDMODE=1 the game creates its top-level "CUBEApp" window with
// WS_POPUP|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX, sized by AdjustWindowRectEx
// around the render resolution and centred on the desktop, then a child
// "CUBERealtime" window that D3D renders into. We patch the main module's
// import of CreateWindowExA and rewrite only the CUBEApp creation: frame bits
// removed, client-sized, placed centred on the primary monitor (exactly at its
// origin when the render resolution equals the monitor resolution).
#include "borderless.h"

#include <windows.h>
#include <string.h>

#include "log.h"

namespace {

using CreateWindowExA_t = HWND(WINAPI*)(DWORD, LPCSTR, LPCSTR, DWORD, int, int, int, int, HWND,
                                        HMENU, HINSTANCE, LPVOID);
CreateWindowExA_t g_realCreateWindowExA = nullptr;

constexpr DWORD kFrameStyles = WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX;
constexpr DWORD kFrameExStyles = WS_EX_DLGMODALFRAME | WS_EX_WINDOWEDGE | WS_EX_CLIENTEDGE | WS_EX_STATICEDGE;

bool IsClass(LPCSTR cls, const char* name) {
  // Class atoms are passed as small integers in the pointer.
  if (reinterpret_cast<ULONG_PTR>(cls) <= 0xFFFF) return false;
  return strcmp(cls, name) == 0;
}

HWND WINAPI HookCreateWindowExA(DWORD exStyle, LPCSTR cls, LPCSTR title, DWORD style, int x, int y,
                                int w, int h, HWND parent, HMENU menu, HINSTANCE inst, LPVOID param) {
  if (!parent && IsClass(cls, "CUBEApp") && (style & WS_CAPTION)) {
    // Undo the AdjustWindowRectEx inflation to recover the render size.
    RECT frame = {0, 0, 0, 0};
    AdjustWindowRectEx(&frame, style, FALSE, exStyle);
    const int clientW = w - (frame.right - frame.left);
    const int clientH = h - (frame.bottom - frame.top);

    MONITORINFO mi = {};
    mi.cbSize = sizeof(mi);
    GetMonitorInfoA(MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY), &mi);
    const RECT& m = mi.rcMonitor;
    const int monW = m.right - m.left;
    const int monH = m.bottom - m.top;
    const int nx = m.left + (monW > clientW ? (monW - clientW) / 2 : 0);
    const int ny = m.top + (monH > clientH ? (monH - clientH) / 2 : 0);

    Log("borderless: CUBEApp %dx%d (window %dx%d) on monitor %dx%d at %d,%d", clientW, clientH, w, h,
        monW, monH, nx, ny);
    if (clientW != monW || clientH != monH)
      Log("borderless: render resolution differs from the monitor; set it to %dx%d in Config.exe for full screen",
          monW, monH);

    style = (style & ~kFrameStyles) | WS_POPUP;
    exStyle &= ~kFrameExStyles;
    return g_realCreateWindowExA(exStyle, cls, title, style, nx, ny, clientW, clientH, parent, menu, inst,
                                 param);
  }
  return g_realCreateWindowExA(exStyle, cls, title, style, x, y, w, h, parent, menu, inst, param);
}

// Replace one import of the main executable. Returns the previous target.
void* PatchImport(const char* dll, const char* func, void* replacement) {
  auto* base = reinterpret_cast<BYTE*>(GetModuleHandleA(nullptr));
  auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
  auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
  const IMAGE_DATA_DIRECTORY& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
  if (!dir.VirtualAddress) return nullptr;
  for (auto* imp = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + dir.VirtualAddress); imp->Name; ++imp) {
    if (_stricmp(reinterpret_cast<char*>(base + imp->Name), dll) != 0) continue;
    auto* names = reinterpret_cast<IMAGE_THUNK_DATA*>(base + imp->OriginalFirstThunk);
    auto* slots = reinterpret_cast<IMAGE_THUNK_DATA*>(base + imp->FirstThunk);
    for (; names->u1.AddressOfData; ++names, ++slots) {
      if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal)) continue;
      auto* byName = reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(base + names->u1.AddressOfData);
      if (strcmp(reinterpret_cast<char*>(byName->Name), func) != 0) continue;
      void* previous = reinterpret_cast<void*>(slots->u1.Function);
      DWORD old;
      VirtualProtect(&slots->u1.Function, sizeof(void*), PAGE_READWRITE, &old);
      slots->u1.Function = reinterpret_cast<ULONG_PTR>(replacement);
      VirtualProtect(&slots->u1.Function, sizeof(void*), old, &old);
      return previous;
    }
  }
  return nullptr;
}

}  // namespace

// Race_Steam.exe 1.2.1.10 (and the Remaster's RaceR_Steam.exe): before creating
// the window the game compares the chosen video mode with the desktop mode and,
// if they differ (e.g. 2560x1440@60 picked on a 165 Hz desktop), silently clears
// the windowed flag: `mov byte ptr [0xa05e60], 0` at 0x5b2b8f. A windowed
// swap chain does not care about the mode's refresh rate, so drop that fallback.
static void KeepWindowedOnModeMismatch() {
  static const BYTE kForceFullscreen[] = {0xc6, 0x05, 0x60, 0x5e, 0xa0, 0x00, 0x00};
  BYTE* site = reinterpret_cast<BYTE*>(0x5b2b8f);
  MEMORY_BASIC_INFORMATION mbi;
  const bool mapped = VirtualQuery(site, &mbi, sizeof(mbi)) == sizeof(mbi) && mbi.State == MEM_COMMIT &&
                      (mbi.Protect & (PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_READONLY | PAGE_READWRITE));
  if (!mapped || memcmp(site, kForceFullscreen, sizeof(kForceFullscreen)) != 0) {
    Log("borderless: fullscreen fallback not found (unknown build), left as is");
    return;
  }
  DWORD old;
  VirtualProtect(site, sizeof(kForceFullscreen), PAGE_EXECUTE_READWRITE, &old);
  memset(site, 0x90, sizeof(kForceFullscreen));
  VirtualProtect(site, sizeof(kForceFullscreen), old, &old);
  FlushInstructionCache(GetCurrentProcess(), site, sizeof(kForceFullscreen));
  Log("borderless: disabled the fullscreen fallback on video mode mismatch");
}

void InstallWindowHooks(const WindowConfig& cfg) {
  if (cfg.dpiAware) {
    // Without this, a 2560x1440 render window on a 125% scaled desktop is
    // treated as 3200x1800 and bitmap-stretched by DWM.
    using SetCtx_t = BOOL(WINAPI*)(HANDLE);
    auto setCtx = reinterpret_cast<SetCtx_t>(
        GetProcAddress(GetModuleHandleA("user32.dll"), "SetProcessDpiAwarenessContext"));
    BOOL ok = setCtx ? setCtx(reinterpret_cast<HANDLE>(-4) /* PER_MONITOR_AWARE_V2 */) : SetProcessDPIAware();
    Log("dpi awareness: %s", ok ? "on" : "unchanged (already set by the game or the OS)");
  }
  if (cfg.borderless) {
    g_realCreateWindowExA = reinterpret_cast<CreateWindowExA_t>(
        PatchImport("user32.dll", "CreateWindowExA", reinterpret_cast<void*>(&HookCreateWindowExA)));
    Log("borderless: CreateWindowExA hook %s", g_realCreateWindowExA ? "installed" : "NOT installed");
    KeepWindowedOnModeMismatch();
  }
}
