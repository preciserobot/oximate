#pragma once

#include "clock_engine.h"
#include "midi_outputs.h"

/*! Note plumbing shared by the clock-driven generators (stage 5 drum machine, stage 12 Gen
 *  engine): note-ons through MidiOutputs with the owner's origin, a note-off after a gate length
 *  via the clock engine's scheduler, and release of everything still sounding on stop.
 *  MIDI task only. */
class StepVoice
{
public:
  enum Outputs : uint8_t
  {
    ToDin = 1,
    ToUsb = 2,
  };

  void begin(MidiOutputs &outputs, ClockEngine &clock, uint8_t owner);

  /*! Note-on now; a note-off `gateMicros` after `timeUs` (0 = the caller ends the note) */
  void note(uint8_t outputs, uint8_t channel, uint8_t note, uint8_t velocity, uint64_t timeUs, uint32_t gateMicros);
  void noteOff(uint8_t outputs, uint8_t channel, uint8_t note);
  void controlChange(uint8_t outputs, uint8_t channel, uint8_t controller, uint8_t value);
  /*! Note-offs for every note of this owner still sounding (stop, mode change) */
  void releaseAll();

  Origin origin() const { return Origin::internal(_owner); }

private:
  void send(uint8_t outputs, const uint8_t *bytes, uint16_t length);
  static void noteOffCallback(void *ctx, uint32_t packed, uint64_t timeUs);

  MidiOutputs *_outputs = nullptr;
  ClockEngine *_clock = nullptr;
  uint8_t _owner = 0;
};
