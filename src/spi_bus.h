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
void lock();
void unlock();
} // namespace SpiBus
#else
namespace SpiBus
{
inline void begin() {}
inline bool tryLock() { return true; }
inline void lock() {}
inline void unlock() {}
} // namespace SpiBus
#endif

/*! Blocking RAII lock, for UI and SD code */
class SpiLock
{
public:
  SpiLock() { SpiBus::lock(); }
  ~SpiLock() { SpiBus::unlock(); }
};
