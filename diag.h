#pragma once
#include <stdint.h>

// Compile-time log level. 0 strips every call. 1 keeps errors and the boot
// line. 2 adds state changes and the 10 s heartbeat. 3 is the same as 2 plus
// PMU IRQ traces. Override with -DTAMAPOKE_LOG=0.
#ifndef TAMAPOKE_LOG
#define TAMAPOKE_LOG 2
#endif

// Runtime switch (NVS "tamapoke"/"log", default on). When off, heartbeat and
// info go silent; errors and the one boot line still print if TAMAPOKE_LOG
// allows them. The settings screen toggles it.
void diagBoot();
bool diagRuntimeOn();
void diagSetRuntime(bool on);

void diagError(const char *msg);
void diagInfo(const char *msg);
void diagFeedWdt();
void diagNoteFrame();

struct DiagBeat {
  int batPct;
  int mv;
  bool charging;
  bool usb;
  uint8_t dim;
  bool screenOff;
  bool locked;
};
void diagHeartbeat(const DiagBeat &b);
