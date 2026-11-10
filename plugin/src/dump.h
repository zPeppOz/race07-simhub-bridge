#pragma once

// Raw memory snapshots used to map game structures (see README, "Mapping").
// Each call appends one record when `interval` seconds have passed.
void DumpOpen(const char* path, float interval);
void DumpTick(double sessionTime);
void DumpClose();
