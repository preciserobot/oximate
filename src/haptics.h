#pragma once

#include <Arduino.h>
#include <stdint.h>
#include "clock_engine.h"
#include "cue_queue.h"
#include "midi_message.h"

/*! Where a group of cues goes */
enum class CueOutput : uint8_t
{
  Off = 0,
  Vibrate = 1,
  Click = 2, ///< a click on the built-in speaker
  Both = 3,
};

/*! Cue groups, each with its own output setting */
enum class CueGroup : uint8_t
{
  Metronome = 0, ///< beat, downbeat, count-in
  ClockAlerts,   ///< clock lost / locked again
  Phrase,        ///< stage 22
  Kick,          ///< "feel the kick"
  Touch,         ///< tick on touch buttons and chips
  Panic,         ///< panic done
  Answers,       ///< ear trainer right / wrong
  Battery,       ///< low battery
  Count,
};

/*! Haptic settings, persisted in NVS (key "haptic"). Bump kVersion when the layout changes. */
struct HapticConfig
{
  static constexpr uint8_t kVersion = 2;

  uint8_t version = kVersion;
  /// Master switch, off by default (decided 2026-11-02): no vibration or click at all until the
  /// user turns cues on (Cues view chip, or hold C on the Clock page)
  bool enabled = false;
  // Per group, used once enabled: metronome vibrates, count-in stays local
  CueOutput outputs[(int)CueGroup::Count] = {CueOutput::Vibrate, CueOutput::Vibrate, CueOutput::Vibrate,
                                              CueOutput::Off,     CueOutput::Off,     CueOutput::Vibrate,
                                              CueOutput::Vibrate, CueOutput::Vibrate};
  uint8_t strength = 235;  ///< motor level at full pattern strength (110-235)
  uint8_t leadMillis = 25; ///< scheduled cues start this early so the felt peak lands on time
  uint8_t kickNote = 36;
  uint8_t kickChannel = 10;
  uint8_t countInBars = 0; ///< 0 = off, 1 or 2 bars of count-in before Start (local only, no MIDI)
};

/*! Plays cues on the vibration motor and/or as a speaker click (stage 10).
 *  UI thread only: the motor sits on the internal I2C bus, which isn't locked.
 *  Cues arrive through Cues::post() from any task (clock listener and kick observer in the MIDI
 *  task, pages and the sketch on the UI side); service() takes them, schedules those with a due
 *  time, applies priorities and the group settings, and advances the playing pattern. */
class Haptics
{
public:
  /*! Hardware hooks (the sketch binds M5.Power.setVibration and SpeakerSynth; tests use fakes) */
  struct Output
  {
    void (*vibrate)(uint8_t level, void *ctx) = nullptr;
    void (*click)(bool accent, void *ctx) = nullptr;
    void *ctx = nullptr;
  };

  static constexpr int kScheduled = 8;

  static CueGroup groupOf(Cue cue);

  void begin(const Output &output, ClockEngine *clock);

  // Settings (UI)
  HapticConfig &config() { return _config; }
  void changed(); ///< saved after a short delay
  void poll();    ///< delayed save

  /*! Advance everything; call every loop and between screen slices */
  void service() { service(micros()); }
  void service(uint32_t nowMicros);

  /*! Play every pattern once, in priority order (Cues view: B) */
  void test();

  bool playing() const { return _playing; }
  Cue current() const { return _current; }

  // MIDI task side
  /*! ClockEngine listener: beats scheduled one period ahead, minus the lead time */
  static void clockListener(const ClockEvent &event, void *ctx);
  /*! InputHooks observer: "feel the kick" */
  static void kickObserver(const MidiMessage &msg, bool forwarded, void *ctx);

private:
  void start(const Cues::Item &item, uint32_t nowMicros);
  void setLevel(uint8_t level);
  void checkClock();
  void load();
  void save();

  Output _output;
  ClockEngine *_clock = nullptr;
  HapticConfig _config;
  uint32_t _changedMillis = 0;
  bool _dirty = false;

  // Pattern player
  bool _playing = false;
  Cue _current = Cue::Beat;
  uint8_t _segment = 0;
  uint32_t _segmentEnd = 0;
  uint16_t _kickMillis = 0;
  uint8_t _level = 0;

  Cues::Item _scheduled[kScheduled];
  int _scheduledCount = 0;

  // Clock alerts, from the engine's snapshot
  bool _wasLost = false;
  bool _wasLocked = false;

  // Test sequence
  int _testIndex = -1;
  uint32_t _testNext = 0;

  // Read by the MIDI task (single bytes, written by the UI)
  volatile uint8_t _leadMillis = 25;
  volatile uint8_t _kickNote = 36;
  volatile uint8_t _kickChannel = 10;
  volatile bool _kickOn = false;
};
