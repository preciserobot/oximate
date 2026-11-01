#pragma once

/*! Short critical sections shared between the MIDI task (core 0) and the UI (core 1).
 *  On the host (unit tests) they compile to nothing. */
#if defined(ESP_PLATFORM)
#include <freertos/FreeRTOS.h>

class CriticalSection
{
public:
  void enter() { portENTER_CRITICAL(&_mux); }
  void exit() { portEXIT_CRITICAL(&_mux); }

private:
  portMUX_TYPE _mux = portMUX_INITIALIZER_UNLOCKED;
};
#else
class CriticalSection
{
public:
  void enter() {}
  void exit() {}
};
#endif

/*! RAII guard for CriticalSection */
class CriticalGuard
{
public:
  explicit CriticalGuard(CriticalSection &section) : _section(section) { _section.enter(); }
  ~CriticalGuard() { _section.exit(); }

private:
  CriticalSection &_section;
};
