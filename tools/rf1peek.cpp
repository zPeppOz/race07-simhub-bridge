// Prints the $rFactorShared$ buffer published by race07bridge.sbp, the same
// way SimHub's GTR2 reader sees it. Usage: rf1peek.exe [count] [intervalMs]
#include <windows.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "../plugin/src/rf1_shared.h"

int main(int argc, char** argv) {
  const int count = argc > 1 ? atoi(argv[1]) : 1;
  const int interval = argc > 2 ? atoi(argv[2]) : 1000;
  HANDLE h = OpenFileMappingA(FILE_MAP_READ, FALSE, rf1::kSharedName);
  if (!h) {
    printf("%s not found (game not running or plugin not loaded)\n", rf1::kSharedName);
    return 1;
  }
  const auto* s = static_cast<const rf1::Shared*>(MapViewOfFile(h, FILE_MAP_READ, 0, 0, sizeof(rf1::Shared)));
  for (int k = 0; k < count; ++k) {
    if (k) Sleep(interval);
    printf("track=%s len=%.1f session=%d phase=%d realtime=%d ET=%.2f maxLaps=%d cars=%d player=%s\n",
           s->trackName, s->lapDist, s->session, s->gamePhase, s->inRealtime, s->currentET, s->maxLaps,
           s->numVehicles, s->playerName);
    printf("  %s gear=%d rpm=%.0f/%.0f speed=%.1f km/h thr=%.2f brk=%.2f clu=%.2f steer=%.2f fuel=%.1f water=%.1f oil=%.1f\n",
           s->vehicleName, s->gear, s->engineRPM, s->engineMaxRPM, s->speed * 3.6f, s->unfilteredThrottle,
           s->unfilteredBrake, s->unfilteredClutch, s->unfilteredSteering, s->fuel, s->engineWaterTemp,
           s->engineOilTemp);
    printf("  localVel=(%.1f %.1f %.1f) yaw=%.1f\n", s->localVel.x, s->localVel.y, s->localVel.z,
           atan2f(s->oriX.z, s->oriZ.z) * 57.296f);
    for (int w = 0; w < 4; ++w) {
      const rf1::Wheel& x = s->wheel[w];
      printf("  wheel%d temps=%.0f/%.0f/%.0f C press=%.1f kPa wear=%.3f brake=%.0f C load=%.0f surf=%d\n", w,
             x.temperature[0] - 273.16f, x.temperature[1] - 273.16f, x.temperature[2] - 273.16f, x.pressure,
             x.wear, x.brakeTemp - 273.16f, x.tireLoad, x.surfaceType);
    }
    for (int i = 0; i < s->numVehicles && i < rf1::kMaxVehicles; ++i) {
      const rf1::VehicleInfo& v = s->vehicle[i];
      printf("  P%-2d %-22s %-24s lap=%d sec=%d dist=%7.1f cur=%.3f/%.3f last=%.3f (%.3f/%.3f) best=%.3f (%.3f/%.3f) pits=%d fin=%d %s\n",
             v.place, v.driverName, v.vehicleName, v.totalLaps, v.sector, v.lapDist, v.curSector1, v.curSector2,
             v.lastLapTime, v.lastSector1, v.lastSector2, v.bestLapTime, v.bestSector1, v.bestSector2, v.inPits,
             v.finishStatus, v.isPlayer ? "<- player" : "");
    }
  }
  return 0;
}
