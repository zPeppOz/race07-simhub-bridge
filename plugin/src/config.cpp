#include "config.h"

#include <windows.h>
#include <stdlib.h>

Config LoadConfig(const char* ini) {
  Config c;
  c.window.borderless = GetPrivateProfileIntA("Window", "Borderless", 1, ini) != 0;
  c.window.dpiAware = GetPrivateProfileIntA("Window", "DpiAware", 1, ini) != 0;
  c.sharedMemory = GetPrivateProfileIntA("SimHub", "SharedMemory", 1, ini) != 0;
  char buf[32];
  GetPrivateProfileStringA("Debug", "DumpInterval", "0", buf, sizeof(buf), ini);
  c.dumpInterval = static_cast<float>(atof(buf));
  return c;
}
