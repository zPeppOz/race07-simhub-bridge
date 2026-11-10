# race07-simhub-bridge

A Race 07 plugin (`race07bridge.sbp`) that:

1. **Gives SimHub full data.** SimHub's own Race 07 reader only uses the game's `$Race$` buffer: player RPM, speed, gear, fuel, a few temperatures, your lap times. No opponents, pedals, pressures, wear, sectors or pit state, so leaderboard, relative, radar, map and pedal overlays stay empty. The plugin reads the game state every frame and publishes it as `$rFactorShared$`, the rFactor 1 shared-memory layout that SimHub's **GTR2** reader consumes.
2. **Borderless windowed mode.** It hooks the game's `CreateWindowExA` and creates the main window without a frame, sized to the render resolution and placed on the primary monitor. It also makes the process DPI aware, so a 2560×1440 window is not stretched on a scaled desktop.

Supported executable: `RaceR_Steam.exe`, the Race Remaster build. The plugin checks for it by code signature and disables data output on any other build.

## Install

1. Build it (`./build.sh`, needs [llvm-mingw](https://github.com/mstorsjo/llvm-mingw)), or take `dist/race07bridge.sbp`.
2. Copy `race07bridge.sbp` and `plugin/race07bridge.ini` into `<Race 07>\plugins\`. The game loads every `plugins\*.sbp` at startup.
3. Set `WINDOWEDMODE=1` in `Documents\SimBin\RACE 07\Config.ini` (or choose windowed in Config.exe). Use the monitor's native resolution so the borderless window covers the screen.
4. In SimHub, select **GTR2** as the game. Leave the default "Telemetry plugin" mode.

`race07bridge.log`, next to the plugin, reports the detected build, the window placement and the shared-memory status.

## Data published

| Area | Fields |
|---|---|
| Session | track name, track length, session type, game phase, session time, lap limit, number of cars, in-realtime |
| Every car | driver, car, position, laps, sector, lap distance, current/last/best sector splits, last/best lap, in pit lane, pit stops, finish status, world position, orientation, local velocity, speed |
| Player | gear, RPM, max RPM, fuel, water/oil temperature, throttle/brake/clutch/steering, local velocity and acceleration |
| Player wheels | tyre temperatures (L/C/R), pressure, wear, brake temperature, load, lateral force, grip, rotation, surface |

Not available in Race 07, so left at zero: flags (yellow per sector, blue), start lights, weather, ambient and track temperature, damage, gaps (SimHub computes these from positions).

## How the plugin hooks in

Race 07 has an undocumented plugin interface. Found by disassembling `RaceR_Steam.exe`:

- At startup the game `LoadLibrary`s each `plugins\*.sbp` and calls the export `GetSBDTDLL()`, which returns an object pointer.
- Every frame it ORs the `GetFlags()` of all plugins (vtable slot 6) and calls the virtual callbacks whose bits are set (`__thiscall`, callee pops the arguments):

| vtable offset | flag | callback |
|---|---|---|
| +0x04 | 0x01 | per-frame session tick |
| +0x08 | 0x02 | player position, orientation, velocity |
| +0x0c | 0x08 | extended player vehicle state |
| +0x10 | 0x10 | camera and listener (two pointers) |
| +0x14 | 0x04 | input state |

The bridge subscribes only to the per-frame tick and reads the game globals directly. Addresses and offsets are in `plugin/src/race07.cpp`.

## Mapping

The game offsets come from the disassembly: the `$Race$` writer, the results-file writer and the plugin callbacks. Each one was then confirmed against live memory. Setting `[Debug] DumpInterval=1` makes the plugin write the game's whole `.data` section to `race07bridge.dump` every second, on a worker thread.

Each record in the dump has this layout:

- `u32 'R7DS'`
- `u32 tickMs`
- `f64 sessionTime`
- `u32 regionCount`
- one block per region: `u32 address`, `u32 length`, the raw bytes

Comparing records against events in the session (lap crossings, a pit stop, braking) pins each field down. The `.data` section is about 10 MB, so leave the dump off during normal play.

`tools/rf1peek.cpp` prints the published buffer, as SimHub sees it, from a Windows console: `rf1peek.exe [count] [intervalMs]`.
