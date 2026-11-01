#pragma once

#include <atomic>
#include "midi_message.h"

/*! Input statistics for the Diagnostics page (stage 16). observe() and run() belong to the
 *  MIDI task; the UI reads the 32-bit counters directly (single aligned words, so a read is
 *  never torn) and asks for a reset with requestReset(). */
class Stats
{
public:
  enum Type
  {
    Notes,       ///< note on/off, poly pressure
    Controllers, ///< CC
    Other,       ///< program change, channel pressure, pitch bend
    Clock,       ///< clock and transport
    SysEx,
    System,      ///< everything else (MTC, active sensing, ...)
    kTypes,
  };

  void observe(const MidiMessage &msg, bool forwarded, uint32_t nowMillis)
  {
    int input = (int)msg.source;
    _counts[input][typeOf(msg)]++;
    if (!forwarded) _filtered[input]++;
    if (msg.isChannel()) _lastSeen[input][msg.channel() - 1] = nowMillis ? nowMillis : 1;
  }

  void run()
  {
    if (_reset.exchange(false)) {
      for (auto &input : _counts)
        for (auto &count : input) count = 0;
      _filtered[0] = _filtered[1] = 0;
    }
  }

  void requestReset() { _reset = true; }

  uint32_t count(int input, int type) const { return _counts[input][type]; }
  uint32_t filtered(int input) const { return _filtered[input]; }
  uint32_t lastSeen(int input, int channel) const { return _lastSeen[input][channel]; } ///< millis, 0 = never

  static Type typeOf(const MidiMessage &msg)
  {
    uint8_t status = msg.status();
    if (status < 0xF0) {
      switch (status & 0xF0) {
        case 0x80:
        case 0x90:
        case 0xA0: return Notes;
        case 0xB0: return Controllers;
        default:   return Other;
      }
    }
    switch (status) {
      case 0xF0: return SysEx;
      case 0xF2:
      case 0xF8:
      case 0xFA:
      case 0xFB:
      case 0xFC: return Clock;
      default:   return System;
    }
  }

private:
  volatile uint32_t _counts[2][kTypes] = {};
  volatile uint32_t _filtered[2] = {};
  volatile uint32_t _lastSeen[2][16] = {};
  std::atomic<bool> _reset{false};
};
