#pragma once

#include "borderless.h"

struct Config {
  WindowConfig window;
  bool sharedMemory;      // publish the GTR2 Crew Chief buffers for SimHub
  float dumpInterval;     // seconds between raw memory snapshots; 0 = off
};

// Reads <pluginDir>\race07bridge.ini; missing keys keep their defaults.
Config LoadConfig(const char* iniPath);
