#pragma once

struct WindowConfig {
  bool borderless;  // strip the frame of the windowed-mode game window
  bool dpiAware;    // make the process DPI aware so the window uses physical pixels
};

// Must run before the game creates its window (plugins load right before it).
void InstallWindowHooks(const WindowConfig& cfg);
