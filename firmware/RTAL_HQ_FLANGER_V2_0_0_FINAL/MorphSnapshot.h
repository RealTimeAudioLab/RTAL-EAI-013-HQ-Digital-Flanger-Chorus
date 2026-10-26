#ifndef RTAL_MORPH_SNAPSHOT_H
#define RTAL_MORPH_SNAPSHOT_H
#include <stdint.h>
// Header is parsed before Arduino IDE 1.8.x generates function prototypes.
// Keep in sync with PARAM_COUNT and ADV_COUNT (checked in the sketch).
enum { MORPH_LEGACY_COUNT = 24, MORPH_ADV_COUNT = 15 };
struct MorphSnapshot {
  uint8_t legacy[MORPH_LEGACY_COUNT];
  uint8_t extra[MORPH_ADV_COUNT];
};
#endif
