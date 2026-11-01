#pragma once
// Host stand-in for the Arduino core, for unit tests
#include <stdint.h>
#include <string.h>
extern uint32_t gFakeMillis;
extern uint32_t gFakeMicros;
inline uint32_t millis() { return gFakeMillis; }
inline uint32_t micros() { return gFakeMicros; }
