#pragma once

/*! The display, the USB host module (MAX3421E) and the SD card share one SPI bus
 *  through different drivers. Every user takes this lock around its bus access.
 *  The MIDI task only ever try-locks, so it never waits for a screen redraw; when it finds the
 *  bus busy, the next blocking lock() from the UI waits up to ~3 ms to let it go first. */
#if defined(ESP_PLATFORM)
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

namespace SpiBus
{
void begin(); ///< create the mutex; call in setup() before starting the MIDI task
bool tryLock();
void lock(const char *holder = "?");
void unlock();
/*! Longest time the UI held the bus since the last reset, and who held it */
uint32_t maxHoldMicros();
const char *maxHolder();
void resetStats();
} // namespace SpiBus
#else
namespace SpiBus
{
inline void begin() {}
inline bool tryLock() { return true; }
inline void lock(const char * = "?") {}
inline void unlock() {}
inline uint32_t maxHoldMicros() { return 0; }
inline const char *maxHolder() { return ""; }
inline void resetStats() {}
} // namespace SpiBus
#endif

/*! Blocking RAII lock, for UI and SD code */
class SpiLock
{
public:
  explicit SpiLock(const char *holder = "?") { SpiBus::lock(holder); }
  ~SpiLock() { SpiBus::unlock(); }
};
