#pragma once

#include <atomic>
#include "clock_engine.h"
#include "midi_outputs.h"
#include "step_voice.h"
#include "sync.h"

enum class DrumStep : uint8_t
{
  Off = 0,
  On = 1,
  Accent = 2,
};

struct DrumLane
{
  uint8_t note;           ///< GM percussion note
  uint8_t velocity;       ///< for normal steps
  uint8_t accentVelocity; ///< for accented steps
  bool muted;
  bool solo;
};

struct DrumPattern
{
  uint32_t on[8];     ///< per lane, bit n = step n (0-31) plays
  uint32_t accent[8]; ///< per lane, bit n = step n is accented
  uint8_t length;     ///< 1-32 steps
};

/*! Everything the drum machine keeps, persisted in NVS (key "drums"). Bump kVersion when the
 *  layout changes. */
struct DrumBank
{
  static constexpr uint8_t kVersion = 1;

  uint8_t version = kVersion;
  uint8_t channel = 10;    ///< output channel of all lanes (10 = GM drums, the SAM2695 drum layer)
  uint8_t padChannel = 10; ///< pad controller input channel: 0 = off, 1-16
  bool usbOut = false;     ///< also send the drums to the USB device
  DrumLane lanes[8] = {{36, 100, 127, false, false}, {38, 100, 127, false, false}, {42, 90, 120, false, false},
                       {46, 90, 120, false, false},  {39, 100, 127, false, false}, {45, 100, 127, false, false},
                       {50, 100, 127, false, false}, {49, 100, 127, false, false}};
  DrumPattern patterns[16] = {};
  uint8_t chain[16] = {};
  uint8_t chainLength = 0; ///< 0 = no chain: the current pattern loops

  DrumBank()
  {
    for (DrumPattern &pattern : patterns) pattern.length = 16;
  }
};

/*! A drum machine (stage 5): 8 lanes x up to 32 steps, 16 patterns, queued pattern switches and
 *  a chain, played on the clock engine's Step events (swing included) in the MIDI task, out on
 *  channel 10 to DIN (and optionally USB) with Origin::internal(5).
 *
 *  A pad controller on the pad channel plays the lanes live (an InputHooks interceptor: notes
 *  that match a lane's note are consumed, so they aren't routed twice) and records into the
 *  pattern while recording is on.
 *
 *  UI side: bank(), the setters, transport and status getters, takeHit(), poll() (delayed save).
 *  MIDI task side: the clock listener, the input interceptor and run(). Step edits from both
 *  sides go through a critical section. */
class DrumMachine
{
public:
  static constexpr int kLanes = 8;
  static constexpr int kSteps = 32;
  static constexpr int kPatterns = 16;
  static constexpr uint8_t kOwner = 5; ///< Origin::internal owner id
  static constexpr uint32_t kSaveDelayMillis = 2000;

  void begin(MidiOutputs &outputs, ClockEngine &clock); ///< before the MIDI task starts

  // UI side: editing
  const DrumBank &bank() const { return _bank; }
  DrumStep step(int pattern, int lane, int step) const;
  void setStep(int pattern, int lane, int step, DrumStep state);
  void setLane(int lane, const DrumLane &settings);
  void setLength(int pattern, uint8_t length);
  void clearPattern(int pattern);
  /*! Euclid fill (stage 12): `hits` spread evenly over the pattern's length into one lane,
   *  keeping accents that land on hits */
  void fillEuclid(int pattern, int lane, int hits, int rotation = 0);
  void setPadChannel(uint8_t channel);
  void setUsbOut(bool on);
  void setChain(const uint8_t *patterns, uint8_t length); ///< length 0 = chain off
  void poll(); ///< saves a while after the last edit, and only when not playing (or idle 2 s)

  // UI side: playback
  void queuePattern(int pattern); ///< starts at the end of the current pattern (now when stopped)
  void setEnabled(bool on);       ///< play along with the clock (A on the Drums page)
  bool enabled() const { return _enabled; }
  void setRecording(bool on) { _recording = on; }
  bool recording() const { return _recording; }
  void audition(int lane); ///< play a lane once
  bool playing() const { return _playing; }
  int playingPattern() const { return _pattern; }
  int queuedPattern() const { return _queued; } ///< -1 = none
  int position() const { return _position; }     ///< step of the current pattern, -1 = stopped
  int chainIndex() const { return _chainIndex; }
  uint32_t hitCount() const { return _hits; }    ///< for LED and page flashes
  /*! UI: drum hits for the built-in speaker (stage 6 speaker output); false when none */
  bool takeHit(uint8_t &note, uint8_t &velocity);

  // MIDI task side
  void run(uint32_t nowMillis); ///< loop hook: auditions
  static void onClock(const ClockEvent &event, void *ctx);
  static bool onInput(const MidiMessage &msg, void *ctx);

private:
  void playStep(uint64_t timeUs);
  void advance();
  void trigger(uint8_t note, uint8_t velocity, uint64_t timeUs, bool scheduleOff);
  uint8_t outputs() const { return StepVoice::ToDin | (_bank.usbOut ? StepVoice::ToUsb : 0); }
  bool laneAudible(int lane) const;
  void pushHit(uint8_t note, uint8_t velocity);
  void changed();
  void load();
  void save();

  MidiOutputs *_outputs = nullptr;
  ClockEngine *_clock = nullptr;
  StepVoice _voice;
  DrumBank _bank;
  mutable CriticalSection _lock; // step bits written by the UI and by recording in the MIDI task

  // Playback state, written by the MIDI task
  std::atomic<bool> _enabled{true};
  std::atomic<bool> _recording{false};
  std::atomic<bool> _playing{false};
  std::atomic<int> _pattern{0};
  std::atomic<int> _queued{-1};
  std::atomic<int> _position{-1};
  std::atomic<int> _chainIndex{0};
  std::atomic<uint32_t> _hits{0};
  std::atomic<int> _auditionLane{-1};
  uint64_t _lastStepUs = 0;
  uint32_t _stepUs = 125000; ///< length of a 16th, from the clock's tempo

  // Hits for the speaker: single producer (MIDI task), single consumer (UI)
  static constexpr int kHitQueue = 32;
  uint8_t _hitNotes[kHitQueue], _hitVelocities[kHitQueue];
  std::atomic<int> _hitHead{0}, _hitTail{0};

  // UI side
  bool _dirty = false;
  uint32_t _changedMillis = 0;
};
