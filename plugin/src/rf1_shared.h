// Layout of the "$rFactorShared$" buffer written by the rFactor 1 shared
// memory plugin, as read by SimHub's default GTR2 reader
// (RfactorReader.dll, rFactor1Data.rfShared, Pack = 1). Sizes and offsets
// were measured with Marshal.SizeOf/OffsetOf against the installed SimHub;
// the static_asserts below pin them.
#pragma once
#include <stddef.h>
#include <stdint.h>

#pragma pack(push, 1)

namespace rf1 {

constexpr const char* kSharedName = "$rFactorShared$";
constexpr int kMaxVehicles = 128;

struct Vec3 {
  float x, y, z;
};

struct Wheel {
  float rotation;  // rad/s, negative when rolling forward
  float suspensionDeflection;
  float rideHeight;
  float tireLoad;  // N
  float lateralForce;
  float gripFract;
  float brakeTemp;       // Kelvin
  float pressure;        // kPa
  float temperature[3];  // Kelvin, left/center/right
  float wear;            // 1.0 = new
  char terrainName[16];
  uint8_t surfaceType;  // 0 dry, 1 wet, 2 grass, 3 dirt, 4 gravel, 5 kerb
  uint8_t flat;
  uint8_t detached;
};

struct VehicleInfo {
  char driverName[32];
  char vehicleName[64];
  int16_t totalLaps;
  int8_t sector;        // 0 = sector 3, 1 = sector 1, 2 = sector 2
  int8_t finishStatus;  // 0 none, 1 finished, 2 dnf, 3 dq
  float lapDist;
  float pathLateral;
  float trackEdge;
  float bestSector1;
  float bestSector2;  // cumulative S1+S2
  float bestLapTime;
  float lastSector1;
  float lastSector2;  // cumulative S1+S2
  float lastLapTime;
  float curSector1;
  float curSector2;  // cumulative S1+S2
  int16_t numPitstops;
  int16_t numPenalties;
  uint8_t isPlayer;
  int8_t control;  // -1 nobody, 0 local player, 1 local AI, 2 remote
  uint8_t inPits;
  uint8_t place;
  char vehicleClass[32];
  float timeBehindNext;
  int32_t lapsBehindNext;
  float timeBehindLeader;
  int32_t lapsBehindLeader;
  float lapStartET;
  Vec3 pos;
  Vec3 localVel;
  Vec3 localAccel;
  Vec3 oriX;
  Vec3 oriY;
  Vec3 oriZ;
  Vec3 localRot;
  Vec3 localRotAccel;
  float speed;
};

struct Shared {
  // Player telemetry
  float deltaTime;
  int32_t lapNumber;
  float lapStartET;
  char vehicleName[64];
  char trackName[64];
  Vec3 pos;
  Vec3 localVel;
  Vec3 localAccel;
  Vec3 oriX;
  Vec3 oriY;
  Vec3 oriZ;
  Vec3 localRot;
  Vec3 localRotAccel;
  float speed;  // m/s
  int32_t gear;  // -1 reverse, 0 neutral
  float engineRPM;
  float engineWaterTemp;  // Celsius
  float engineOilTemp;    // Celsius
  float clutchRPM;
  float unfilteredThrottle;
  float unfilteredBrake;
  float unfilteredSteering;
  float unfilteredClutch;
  float steeringArmForce;
  float fuel;
  float engineMaxRPM;
  uint8_t scheduledStops;
  uint8_t overheating;
  uint8_t detached;
  uint8_t dentSeverity[8];
  float lastImpactET;
  float lastImpactMagnitude;
  Vec3 lastImpactPos;
  Wheel wheel[4];  // FL, FR, RL, RR
  // Scoring
  int32_t session;  // 0 test day, 1-4 practice, 5-8 qualify, 9 warmup, 10-13 race
  float currentET;
  float endET;
  int32_t maxLaps;
  float lapDist;  // track length
  int32_t numVehicles;
  uint8_t gamePhase;  // 0 garage, 4 countdown, 5 green, 8 session over
  int8_t yellowFlagState;
  int8_t sectorFlag[3];
  uint8_t startLight;
  uint8_t numRedLights;
  uint8_t inRealtime;
  char playerName[32];
  char plrFileName[64];
  float ambientTemp;
  float trackTemp;
  Vec3 wind;
  VehicleInfo vehicle[kMaxVehicles];
};

}  // namespace rf1

#pragma pack(pop)

static_assert(sizeof(rf1::Wheel) == 67, "Wheel");
static_assert(sizeof(rf1::VehicleInfo) == 304, "VehicleInfo");
static_assert(offsetof(rf1::VehicleInfo, vehicleClass) == 152, "vehicleClass");
static_assert(offsetof(rf1::VehicleInfo, pos) == 204, "pos");
static_assert(offsetof(rf1::Shared, speed) == 236, "speed");
static_assert(offsetof(rf1::Shared, lastImpactPos) == 307, "lastImpactPos");
static_assert(offsetof(rf1::Shared, wheel) == 319, "wheel");
static_assert(offsetof(rf1::Shared, session) == 587, "session");
static_assert(offsetof(rf1::Shared, gamePhase) == 611, "gamePhase");
static_assert(offsetof(rf1::Shared, playerName) == 619, "playerName");
static_assert(offsetof(rf1::Shared, vehicle) == 735, "vehicle");
static_assert(sizeof(rf1::Shared) == 39647, "Shared");
