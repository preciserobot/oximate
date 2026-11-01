#pragma once

#include "midi_outputs.h"
#include "note_tracker.h"
#include "sync.h"

class ClockEngine;
class UsbMidi;

/*! Panic and automatic note release (stage 16). Runs in the MIDI task; request() and
 *  summary() may be called from the UI.
 *
 *  Levels: 1 = note-offs for tracked notes (+ pedal up), 2 = plus CC123/CC120/CC64=0 on all
 *  channels, 3 = plus a note-off for every note on every channel. Every panic also sends
 *  Stop. Messages are fed as the output queues have room, so MIDI input keeps flowing.
 *
 *  Automatic release: notes from a USB device that is unplugged, notes from DIN when its
 *  Active Sensing stops, and notes held longer than kStuckMillis. */
class Panic
{
public:
  static constexpr uint32_t kStuckMillis = 30000;       ///< auto-release (decided 2026-11-01)
  static constexpr uint32_t kActiveSensingMillis = 300; ///< MIDI spec
  static constexpr uint8_t kOwner = 16;                 ///< Origin::internal owner id

  struct Summary
  {
    uint16_t held[2];    ///< held + sustained notes per output
    uint16_t sustain[2]; ///< channels with the pedal down per output
    uint32_t panics;     ///< completed panic requests
    uint32_t autoReleased;
    bool active;         ///< a panic is being sent
    uint8_t progress;    ///< 0-100 while active
    uint8_t level;       ///< level of the running or last panic
  };

  void begin(MidiOutputs &outputs, NoteTracker &tracker, UsbMidi &usb, ClockEngine &clock);

  void request(uint8_t level); ///< any task; a higher level replaces a pending lower one
  Summary summary() const;     ///< any task

  // MIDI task
  void observe(const MidiMessage &msg); ///< every input message
  void run(uint32_t nowMillis);         ///< every loop

private:
  struct Cursor
  {
    uint16_t index; ///< next message: 0-47 all-off controllers, then 48 + channel * 128 + note
    bool done;
  };
  static constexpr uint16_t kAllOffMessages = 16 * 3;
  static constexpr uint16_t kBruteMessages = 16 * 128;

  void startJob(uint8_t level);
  void feed(MidiOutput out);
  void releaseFrom(Origin origin);
  void updateSummary();

  MidiOutputs *_outputs = nullptr;
  NoteTracker *_tracker = nullptr;
  UsbMidi *_usb = nullptr;
  ClockEngine *_clock = nullptr;

  mutable CriticalSection _lock;
  uint8_t _requested = 0; // guarded by _lock
  Summary _summary = {};  // guarded by _lock

  // MIDI task state
  uint8_t _level = 0; ///< running job level, 0 = idle
  Cursor _cursor[2] = {};
  uint32_t _panics = 0;
  uint32_t _autoReleased = 0;
  bool _usbWasConnected = false;
  bool _dinSensing = false;
  uint32_t _lastSensingMillis = 0;
  uint32_t _lastStuckCheck = 0;
  uint32_t _lastSummary = 0;
};
