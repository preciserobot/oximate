#pragma once

#include "midi_message.h"

/*! Input hooks in the MIDI task (README "Shared infrastructure"):
 *  - interceptors see every input message before routing and may consume it (it is then not
 *    routed), e.g. a pad controller playing the drum machine, or the Oxi One / Push 2 remotes;
 *  - observers see every input message after routing, with whether it was forwarded, e.g. the
 *    clock analyser or the chord detector.
 *  Both are called in the MIDI task: keep them short, no SPI, no I2C, no Serial.
 *  Register everything in setup(), before the MIDI task starts. */
namespace InputHooks
{
using Interceptor = bool (*)(const MidiMessage &msg, void *ctx); ///< return true to consume
using Observer = void (*)(const MidiMessage &msg, bool forwarded, void *ctx);
/*! Raw SysEx bytes as they arrive (F0, data, F7), before the parsers' 1024-byte assembly limit:
 *  the SysEx librarian (stage 14) captures dumps of any size through it. Realtime bytes inside a
 *  SysEx are not passed on. One tap. */
using SysExTap = void (*)(MidiSource source, const uint8_t *bytes, uint16_t length, void *ctx);

constexpr int kMax = 8;

struct Registry
{
  Interceptor interceptors[kMax];
  void *interceptorCtx[kMax];
  int interceptorCount = 0;
  Observer observers[kMax];
  void *observerCtx[kMax];
  int observerCount = 0;
  SysExTap sysexTap = nullptr;
  void *sysexCtx = nullptr;
};

inline Registry &registry()
{
  static Registry r;
  return r;
}

inline void addInterceptor(Interceptor interceptor, void *ctx)
{
  Registry &r = registry();
  if (r.interceptorCount < kMax) {
    r.interceptors[r.interceptorCount] = interceptor;
    r.interceptorCtx[r.interceptorCount++] = ctx;
  }
}

inline void addObserver(Observer observer, void *ctx)
{
  Registry &r = registry();
  if (r.observerCount < kMax) {
    r.observers[r.observerCount] = observer;
    r.observerCtx[r.observerCount++] = ctx;
  }
}

/*! True if an interceptor consumed the message */
inline bool intercept(const MidiMessage &msg)
{
  Registry &r = registry();
  for (int i = 0; i < r.interceptorCount; i++) {
    if (r.interceptors[i](msg, r.interceptorCtx[i])) return true;
  }
  return false;
}

inline void observe(const MidiMessage &msg, bool forwarded)
{
  Registry &r = registry();
  for (int i = 0; i < r.observerCount; i++) r.observers[i](msg, forwarded, r.observerCtx[i]);
}

inline void setSysExTap(SysExTap tap, void *ctx)
{
  registry().sysexCtx = ctx;
  registry().sysexTap = tap;
}

/*! Called by the DIN and USB parsers with SysEx bytes as they arrive */
inline void sysex(MidiSource source, const uint8_t *bytes, uint16_t length)
{
  Registry &r = registry();
  if (r.sysexTap) r.sysexTap(source, bytes, length, r.sysexCtx);
}
} // namespace InputHooks
