// Race 07 game state -> rFactor 1 shared memory layout.
//
// Offsets were found by disassembling Race_Steam.exe 1.2.1.10 (the $Race$ buffer
// writer, the results-file writer, the plugin callbacks) and confirmed
// against memory snapshots of a live race (Debug.DumpInterval, see README).
#include "race07.h"

#include <windows.h>
#include <math.h>
#include <string.h>

#include "log.h"

namespace race07 {
namespace {

// Globals (Race_Steam.exe 1.2.1.10; the Race Remaster mod's RaceR_Steam.exe is
// the same build with a few byte patches, so the addresses are shared).
namespace g {
constexpr uintptr_t kPlayerVehicle = 0xa08494;  // Vehicle* of the local player
constexpr uintptr_t kVehicleTable = 0xa084a0;   // Vehicle*[numVehicles]
constexpr uintptr_t kNumVehicles = 0x823ba4;    // int
constexpr uintptr_t kCurrentET = 0x115edf8;     // double, session time
constexpr uintptr_t kSession = 0xd6a578;        // int: 0 test, 1-2 practice, 3-4 qualify, 5 warmup, 6-7 race
constexpr uintptr_t kGamePhase = 0xd6a588;      // int, ISI phases (2 grid, 3 formation, 5 green ...)
constexpr uintptr_t kSessionLaps = 0x1154978;   // int, 0x7fffffff when not limited
constexpr uintptr_t kTrackLength = 0x81c5d0;    // float, metres
constexpr uintptr_t kTrackAiwPath = 0x81c4a4;   // char[], GAMEDATA\LOCATIONS\<track>\<layout>.AIW
constexpr uintptr_t kPlayerPhysics = 0xd6c960;  // player physics object (doubles)
}  // namespace g

// Vehicle object, static array of 0x83a0-byte entries.
namespace veh {
constexpr int kOrientation = 0x0e94;     // float[3][3], row-major, local -> world, forward = -column 2
constexpr int kPos = 0x0ec8;             // float[3], world position
constexpr int kWorldVel = 0x503c;        // float[3]
constexpr int kLocalAccel = 0x5094;      // float[3]
constexpr int kWheelSurface = 0x51c4;    // Surface** per wheel, stride 0xfc
constexpr int kFuel = 0x54f0;            // float, litres
constexpr int kEngineRPS = 0x5510;       // float, rad/s
constexpr int kEngineMaxRPS = 0x5514;    // float, rad/s
constexpr int kGear = 0x556c;            // int
constexpr int kLapsCompleted = 0x70f4;   // int
constexpr int kSector = 0x70f8;          // int, 0-based
constexpr int kLapDist = 0x70fc;         // float, metres from the start line
constexpr int kCurSector1 = 0x710c;      // float, <= 0 until crossed
constexpr int kCurSector2 = 0x7110;      // float, cumulative S1+S2
constexpr int kPlace = 0x711c;           // int, 1-based
constexpr int kFinishStatus = 0x715c;    // int, 0 none, 1 finished, 2 dnf, 3 dq
constexpr int kLapStartET = 0x71c0;      // float
constexpr int kBestLap = 0x71c4;         // float, <= 0 when none
constexpr int kInPitLane = 0x1c30;      // int, 1 between pit entry and exit
constexpr int kNumPitstops = 0x7164;    // int, completed stops
constexpr int kLapTimesCount = 0x71f0;   // int
constexpr int kLapTimes = 0x71f8;        // float* per completed lap
constexpr int kDriverNames = 0x7a38;     // char[0x6c] per driver
constexpr int kDriverIndex = 0x7b14;     // int, current driver
constexpr int kVehicleName = 0x7b28;     // char[64]
constexpr int kSlotId = 0x7dc4;          // int
}  // namespace veh

// Player physics object (all doubles).
namespace phys {
constexpr int kSteering = 0x40;  // raw driver inputs
constexpr int kThrottle = 0x48;
constexpr int kBrake = 0x50;
constexpr int kClutch = 0x20;  // applied clutch
constexpr int kOilTemp = 0x2480;    // Kelvin
constexpr int kWaterTemp = 0x24c0;  // Kelvin
constexpr int kWheels = 0x420;      // 4 x 0x510, FL FR RL RR
constexpr int kWheelStride = 0x510;
}  // namespace phys

namespace wheel {
constexpr int kRotation = 0x10;    // rad/s
constexpr int kTireLoad = 0x90;    // N
constexpr int kGripFract = 0x98;
constexpr int kLateralForce = 0xa8;
constexpr int kTemps = 0x310;      // 3 x Kelvin (inner, middle, outer)
constexpr int kPressure = 0x338;   // kPa
constexpr int kWear = 0x358;       // 1.0 = new
constexpr int kBrakeTemp = 0x3e0;  // Kelvin
}  // namespace wheel

constexpr float kKelvin = 273.16f;
constexpr float kRadToRpm = 9.5492966f;

template <typename T>
T At(uintptr_t addr) {
  return *reinterpret_cast<const T*>(addr);
}

void CopyStr(char* dst, size_t n, const char* src) {
  strncpy(dst, src, n - 1);
  dst[n - 1] = '\0';
}

bool CodeMatches(uintptr_t addr, const char* hex) {
  const auto* p = reinterpret_cast<const unsigned char*>(addr);
  auto nib = [](char c) { return c <= '9' ? c - '0' : (c | 0x20) - 'a' + 10; };
  for (size_t i = 0; hex[2 * i]; ++i)
    if (p[i] != (nib(hex[2 * i]) << 4 | nib(hex[2 * i + 1]))) return false;
  return true;
}

// Sector times of finished laps are not kept by the game; derive them from
// the running sector splits as each lap completes.
struct LapBook {
  int laps = -1;
  float runS1 = -1, runS2 = -1;  // splits seen during the current lap
  float lastS1 = -1, lastS2 = -1, lastLap = -1;
  float bestS1 = -1, bestS2 = -1, bestLap = -1;
};
LapBook g_books[rf1::kMaxVehicles];
int g_bookSession = -1;
double g_bookET = 0;

double g_lastET = -1;
DWORD g_lastETChange = 0;

void ResetBooks() {
  for (LapBook& b : g_books) b = LapBook();
}

void UpdateBook(LapBook& b, int laps, float curS1, float curS2, float lapTime) {
  if (b.laps >= 0 && laps == b.laps + 1) {
    b.lastLap = lapTime;
    b.lastS1 = b.runS1;
    b.lastS2 = b.runS2;
    if (lapTime > 0 && (b.bestLap <= 0 || lapTime < b.bestLap)) {
      b.bestLap = lapTime;
      b.bestS1 = b.runS1;
      b.bestS2 = b.runS2;
    }
    b.runS1 = b.runS2 = -1;
  } else if (laps != b.laps) {
    b.runS1 = b.runS2 = -1;  // joined mid-session or lap counter reset
  }
  b.laps = laps;
  if (curS1 > 0) b.runS1 = curS1;
  if (curS2 > 0) b.runS2 = curS2;
}

// rFactor numbering: 0 test day, 1-4 practice, 5-8 qualify, 9 warmup, 10-13 race.
int RfSession(int s) {
  switch (s) {
    case 0: return 0;
    case 1: return 1;
    case 2: return 2;
    case 3: return 5;
    case 4: return 6;
    case 5: return 9;
    case 6: return 10;
    case 7: return 11;
    default: return 0;
  }
}

// Same mapping the game uses for its own plugin callback.
uint8_t SurfaceType(uintptr_t vehicle, int w) {
  const uintptr_t surf = At<uintptr_t>(vehicle + veh::kWheelSurface + w * 0xfc);
  if (!surf) return 0;
  const uintptr_t mat = At<uintptr_t>(surf + 0x54);
  if (!mat) return 0;
  switch (At<int>(mat + 0x30)) {
    case 0x17: case 0x18: return 1;
    case 0x19: return 2;
    case 0x1a: return 4;
    case 0x1b: return 3;
    case 0x1c: return 5;
    default: return 0;
  }
}

void TrackName(char* out, size_t n) {
  // "GAMEDATA\LOCATIONS\Curitiba_2007\2007_Curitiba.AIW" -> "Curitiba_2007"
  const char* path = reinterpret_cast<const char*>(g::kTrackAiwPath);
  out[0] = '\0';
  if (_strnicmp(path, "GAMEDATA\\LOCATIONS\\", 19) != 0) return;
  const char* start = path + 19;
  const char* end = strchr(start, '\\');
  size_t len = end ? static_cast<size_t>(end - start) : strlen(start);
  if (len >= n) len = n - 1;
  memcpy(out, start, len);
  out[len] = '\0';
}

void Orientation(uintptr_t v, rf1::Vec3& x, rf1::Vec3& y, rf1::Vec3& z) {
  const float* m = reinterpret_cast<const float*>(v + veh::kOrientation);
  x = {m[0], m[1], m[2]};
  y = {m[3], m[4], m[5]};
  z = {m[6], m[7], m[8]};
}

// World velocity into the car frame (transpose of the orientation matrix).
rf1::Vec3 LocalVelocity(uintptr_t v) {
  const float* m = reinterpret_cast<const float*>(v + veh::kOrientation);
  const float* w = reinterpret_cast<const float*>(v + veh::kWorldVel);
  return {m[0] * w[0] + m[3] * w[1] + m[6] * w[2], m[1] * w[0] + m[4] * w[1] + m[7] * w[2],
          m[2] * w[0] + m[5] * w[1] + m[8] * w[2]};
}

void FillWheel(uintptr_t vehicle, int i, rf1::Wheel& out) {
  const uintptr_t w = g::kPlayerPhysics + phys::kWheels + i * phys::kWheelStride;
  out.rotation = static_cast<float>(At<double>(w + wheel::kRotation));
  out.tireLoad = static_cast<float>(At<double>(w + wheel::kTireLoad));
  out.gripFract = static_cast<float>(At<double>(w + wheel::kGripFract));
  out.lateralForce = static_cast<float>(At<double>(w + wheel::kLateralForce));
  out.brakeTemp = static_cast<float>(At<double>(w + wheel::kBrakeTemp));
  out.pressure = static_cast<float>(At<double>(w + wheel::kPressure));
  out.wear = static_cast<float>(At<double>(w + wheel::kWear));
  // Game order is inner/middle/outer; rFactor wants left/center/right.
  const double inner = At<double>(w + wheel::kTemps);
  const double middle = At<double>(w + wheel::kTemps + 8);
  const double outer = At<double>(w + wheel::kTemps + 16);
  const bool leftSide = (i % 2) == 0;
  out.temperature[0] = static_cast<float>(leftSide ? outer : inner);
  out.temperature[1] = static_cast<float>(middle);
  out.temperature[2] = static_cast<float>(leftSide ? inner : outer);
  out.surfaceType = SurfaceType(vehicle, i);
}

}  // namespace

bool Init() {
  // The function that writes the game's own $Race$ buffer from the player vehicle.
  if (CodeMatches(0x53e1f0, "51a19484a00053558ba8f4700000568bf1")) {
    Log("game build: Race 07 Steam 1.2.1.10");
    return true;
  }
  return false;
}

bool Read(rf1::Shared& s) {
  const uintptr_t player = At<uintptr_t>(g::kPlayerVehicle);
  const int n = At<int>(g::kNumVehicles);
  if (!player || n <= 0 || n > rf1::kMaxVehicles) return false;

  memset(&s, 0, sizeof(s));
  const double et = At<double>(g::kCurrentET);
  const int session = At<int>(g::kSession);
  if (session != g_bookSession || et < g_bookET) {
    ResetBooks();
    g_bookSession = session;
  }
  g_bookET = et;
  const DWORD now = GetTickCount();
  if (et != g_lastET) {
    g_lastET = et;
    g_lastETChange = now;
  }

  // Session
  s.session = RfSession(session);
  s.currentET = static_cast<float>(et);
  const int laps = At<int>(g::kSessionLaps);
  s.maxLaps = (laps > 0 && laps < 1000) ? laps : 0;
  s.lapDist = At<float>(g::kTrackLength);
  s.numVehicles = n;
  s.gamePhase = static_cast<uint8_t>(At<int>(g::kGamePhase));
  s.inRealtime = now - g_lastETChange < 500;  // time stops in menus and when paused
  TrackName(s.trackName, sizeof(s.trackName));

  // Vehicles
  for (int i = 0; i < n; ++i) {
    const uintptr_t v = At<uintptr_t>(g::kVehicleTable + 4 * i);
    rf1::VehicleInfo& o = s.vehicle[i];
    const int driver = At<int>(v + veh::kDriverIndex);
    CopyStr(o.driverName, sizeof(o.driverName),
            reinterpret_cast<const char*>(v + veh::kDriverNames + driver * 0x6c));
    CopyStr(o.vehicleName, sizeof(o.vehicleName), reinterpret_cast<const char*>(v + veh::kVehicleName));
    o.isPlayer = v == player;
    o.control = o.isPlayer ? 0 : 1;
    const int lapsDone = At<int>(v + veh::kLapsCompleted);
    o.totalLaps = static_cast<int16_t>(lapsDone);
    const int sector = At<int>(v + veh::kSector);
    o.sector = static_cast<int8_t>(sector == 2 ? 0 : sector + 1);
    o.finishStatus = static_cast<int8_t>(At<int>(v + veh::kFinishStatus));
    o.lapDist = At<float>(v + veh::kLapDist);
    o.place = static_cast<uint8_t>(At<int>(v + veh::kPlace));
    o.lapStartET = At<float>(v + veh::kLapStartET);
    o.inPits = At<int>(v + veh::kInPitLane) != 0;
    o.numPitstops = static_cast<int16_t>(At<int>(v + veh::kNumPitstops));
    // The game keeps the previous lap's splits until the car crosses the
    // next sector line, so a split only belongs to this lap once passed.
    const float split1 = At<float>(v + veh::kCurSector1);
    const float split2 = At<float>(v + veh::kCurSector2);
    o.curSector1 = (sector >= 1 && split1 > 0) ? split1 : -1.0f;
    o.curSector2 = (sector >= 2 && split2 > 0) ? split2 : -1.0f;

    const auto* lapTimes = At<const float*>(v + veh::kLapTimes);
    const bool haveLast = lapsDone > 0 && lapTimes && lapsDone <= At<int>(v + veh::kLapTimesCount);
    const float lastLap = haveLast ? lapTimes[lapsDone - 1] : -1.0f;
    const int slot = At<int>(v + veh::kSlotId);
    LapBook& book = g_books[(slot >= 0 && slot < rf1::kMaxVehicles) ? slot : i];
    UpdateBook(book, lapsDone, o.curSector1, o.curSector2, lastLap);
    o.lastLapTime = lastLap > 0 ? lastLap : -1.0f;
    o.lastSector1 = book.lastS1;
    o.lastSector2 = book.lastS2;
    const float best = At<float>(v + veh::kBestLap);
    o.bestLapTime = best > 0 ? best : -1.0f;
    o.bestSector1 = book.bestS1;
    o.bestSector2 = book.bestS2;

    memcpy(&o.pos, reinterpret_cast<const void*>(v + veh::kPos), sizeof(o.pos));
    memcpy(&o.localAccel, reinterpret_cast<const void*>(v + veh::kLocalAccel), sizeof(o.localAccel));
    Orientation(v, o.oriX, o.oriY, o.oriZ);
    o.localVel = LocalVelocity(v);
    o.speed = sqrtf(o.localVel.x * o.localVel.x + o.localVel.y * o.localVel.y + o.localVel.z * o.localVel.z);
  }

  // Player telemetry
  CopyStr(s.vehicleName, sizeof(s.vehicleName), reinterpret_cast<const char*>(player + veh::kVehicleName));
  {
    const int driver = At<int>(player + veh::kDriverIndex);
    CopyStr(s.playerName, sizeof(s.playerName),
            reinterpret_cast<const char*>(player + veh::kDriverNames + driver * 0x6c));
  }
  s.lapNumber = At<int>(player + veh::kLapsCompleted);
  s.lapStartET = At<float>(player + veh::kLapStartET);
  memcpy(&s.pos, reinterpret_cast<const void*>(player + veh::kPos), sizeof(s.pos));
  memcpy(&s.localAccel, reinterpret_cast<const void*>(player + veh::kLocalAccel), sizeof(s.localAccel));
  Orientation(player, s.oriX, s.oriY, s.oriZ);
  s.localVel = LocalVelocity(player);
  s.speed = sqrtf(s.localVel.x * s.localVel.x + s.localVel.y * s.localVel.y + s.localVel.z * s.localVel.z);
  s.gear = At<int>(player + veh::kGear);
  s.engineRPM = At<float>(player + veh::kEngineRPS) * kRadToRpm;
  s.engineMaxRPM = At<float>(player + veh::kEngineMaxRPS) * kRadToRpm;
  s.fuel = At<float>(player + veh::kFuel);
  const uintptr_t p = g::kPlayerPhysics;
  s.engineWaterTemp = static_cast<float>(At<double>(p + phys::kWaterTemp)) - kKelvin;
  s.engineOilTemp = static_cast<float>(At<double>(p + phys::kOilTemp)) - kKelvin;
  s.unfilteredThrottle = static_cast<float>(At<double>(p + phys::kThrottle));
  s.unfilteredBrake = static_cast<float>(At<double>(p + phys::kBrake));
  s.unfilteredSteering = static_cast<float>(At<double>(p + phys::kSteering));
  s.unfilteredClutch = static_cast<float>(At<double>(p + phys::kClutch));
  for (int w = 0; w < 4; ++w) FillWheel(player, w, s.wheel[w]);
  return true;
}

}  // namespace race07
