#pragma once

#include <stdint.h>

/*! 64-bit microsecond clock shared by the MIDI task and the clock engine.
 *  On the ESP32 it is esp_timer's time base (the same counter micros() reads);
 *  in host tests it is driven by the test. */
#if defined(ESP_PLATFORM)
#include <esp_timer.h>
inline uint64_t nowMicros64() { return esp_timer_get_time(); }
#else
extern uint64_t gFakeMicros64;
inline uint64_t nowMicros64() { return gFakeMicros64; }
#endif
