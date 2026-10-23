#pragma once
#include <Arduino.h>
// Keep in sync with the parameter layout; versioned SD format.
struct FluxPreset { uint32_t magic; uint16_t version; uint16_t count; uint8_t params[24]; uint32_t checksum; };
