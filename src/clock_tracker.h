#pragma once

#include <Arduino.h>
#include "midi_message.h"

/*! Follows incoming MIDI clock per input: BPM (averaged per quarter note) and transport */
class ClockTracker
{
public:
  static constexpr uint32_t kTimeoutMillis = 2000;

  struct State
  {
    uint32_t lastTickMillis = 0;
    uint32_t beatStartMicros = 0;
    uint8_t ticks = 0;
    float bpm = 0;
    bool running = false;

    bool active() const { return lastTickMillis && millis() - lastTickMillis < kTimeoutMillis; }
  };

  void track(const MidiMessage &msg)
  {
    State &clock = _state[(int)msg.source];
    switch (msg.status()) {
      case 0xF8: {
        uint32_t now = msg.timeMicros ? msg.timeMicros : micros(); // stamped at the input
        clock.lastTickMillis = millis();
        if (clock.ticks == 0) {
          clock.beatStartMicros = now;
        } else if (clock.ticks == 24) { // 24 ticks per quarter note
          clock.bpm = 60e6f / (now - clock.beatStartMicros);
          clock.beatStartMicros = now;
          clock.ticks = 0;
        }
        clock.ticks++;
        break;
      }
      case 0xFA:
        clock.ticks = 0;
        clock.running = true;
        break;
      case 0xFB:
        clock.running = true;
        break;
      case 0xFC:
        clock.running = false;
        break;
    }
  }

  const State &state(MidiSource source) const { return _state[(int)source]; }

  /*! The input that ticked most recently within the timeout, or nullptr */
  const State *latest(MidiSource &source) const
  {
    const State *best = nullptr;
    for (int i = 0; i < 2; i++) {
      if (_state[i].active() && (!best || _state[i].lastTickMillis > best->lastTickMillis)) {
        best = &_state[i];
        source = (MidiSource)i;
      }
    }
    return best;
  }

private:
  State _state[2];
};
