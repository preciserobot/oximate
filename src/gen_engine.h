#pragma once

#include <atomic>
#include "clock_engine.h"
#include "gen_core.h"
#include "midi_outputs.h"
#include "music_theory.h"
#include "step_voice.h"
#include "sync.h"

enum class GenKind : uint8_t
{
  Off,
  Euclid,
  Turing,
  Mod,
  Count,
};

/*! How often a slot steps. 1/16, 1/8 and 1/4 follow the clock's (swung) Step events; 1/32 and
 *  1/16T count clock ticks and stay straight. */
enum class GenRate : uint8_t
{
  R32,
  R16,
  R8,
  R4,
  R16T,
  Count,
};

/*! One generator slot. All kinds share the first block; each kind uses its own fields. */
struct GenSlot
{
  GenKind kind = GenKind::Off;
  bool muted = true; ///< new slots start muted (decided defaults, see README stage 12)
  uint8_t outputs = StepVoice::ToDin;
  uint8_t channel = 10;
  GenRate rate = GenRate::R16;
  uint8_t probability = 100; ///< % chance that a step plays
  uint8_t gate = 50;         ///< note length, % of the slot's step
  uint8_t velocity = 100;

  // Euclid
  uint8_t steps = 16, hits = 4, rotation = 0;
  uint8_t note = 36;
  uint8_t accentHits = 0, accentRotation = 0; ///< a second Euclid: +27 velocity on its hits

  // Turing
  uint8_t length = 8;
  uint8_t mutate = 10; ///< % chance per step that the value coming round changes
  bool locked = false; ///< B: mutation frozen
  uint8_t scale = MusicTheory::ScaleMinor;
  uint8_t root = 0;   ///< pitch class
  uint8_t octave = 3; ///< base octave (C3 = 48)
  uint8_t range = 1;  ///< octaves
  uint8_t density = 70;
  uint32_t seed = 1;

  // Mod lane
  uint8_t cc = 74;
  ModShape shape = ModShape::Sine;
  uint16_t period = 16; ///< steps (16ths) per cycle, 1-1024
  uint8_t min = 0, max = 127;
};

/*! Generator settings, persisted in NVS (key "gen"). Bump kVersion when the layout changes. */
struct GenConfig
{
  static constexpr uint8_t kVersion = 1;
  static constexpr int kSlots = 8;

  uint8_t version = kVersion;
  GenSlot slots[kSlots];

  GenConfig();
};

/*! What the page shows of a slot (copied under the lock by the MIDI task) */
struct GenSlotStatus
{
  uint32_t position;  ///< steps played since start or realign
  int16_t lastNote;   ///< -1 = rest / none yet
  uint8_t modValue;
  uint8_t registerValues[TuringRegister::kMaxLength];
  uint8_t registerPosition;
  uint32_t triggers;  ///< notes sent (page and LED flashes)
};

/*! Generative sequencers (stage 12): 8 slots of Euclid rhythms, mutating Turing sequences and
 *  CC mod lanes, played on the clock engine in the MIDI task alongside the drum machine.
 *  UI side: config()/changed()/poll(), realign(), status(). MIDI task side: onClock(), run(). */
class GenEngine
{
public:
  static constexpr int kSlots = GenConfig::kSlots;
  static constexpr uint8_t kOwner = 12; ///< Origin::internal owner id
  static constexpr uint32_t kSaveDelayMillis = 2000;
  static constexpr uint8_t kAccentBoost = 27;

  void begin(MidiOutputs &outputs, ClockEngine &clock); ///< before the MIDI task starts

  // UI side
  GenConfig &config() { return _edit; }
  void changed(); ///< hand config() to the MIDI task; saved a while later (only when stopped)
  void poll();
  void realign(); ///< hold A: every slot restarts at step 1 on the next bar
  GenSlotStatus status(int slot) const;
  bool playing() const { return _playing; }

  // MIDI task side
  static void onClock(const ClockEvent &event, void *ctx);
  void run(uint32_t nowMillis); ///< loop hook: picks up settings changes between steps

  /*! Clock ticks (24 PPQN) per step of a rate */
  static int ticksPerStep(GenRate rate);

private:
  void applyPending();
  void trigger(int slot, uint64_t timeUs, uint32_t stepUs);
  void updateMod(int slot);
  void restart();
  void load();
  void save();

  MidiOutputs *_outputs = nullptr;
  ClockEngine *_clock = nullptr;
  StepVoice _voice;

  // UI side
  GenConfig _edit;
  bool _dirty = false;
  uint32_t _changedMillis = 0;

  // Handoff (guarded by _lock)
  mutable CriticalSection _lock;
  GenConfig _pending;
  bool _pendingSet = false;
  GenSlotStatus _status[kSlots] = {};

  // MIDI task side
  GenConfig _active;
  std::atomic<bool> _playing{false};
  std::atomic<bool> _realign{false};
  uint32_t _tickUs = 20833; ///< length of a clock tick, from the tempo
  uint32_t _position[kSlots] = {};
  uint32_t _modTick[kSlots] = {};
  float _modFrom[kSlots] = {}, _modTo[kSlots] = {};
  int16_t _lastCc[kSlots];
  TuringRegister _turing[kSlots];
  XorShift32 _rng[kSlots];
};
