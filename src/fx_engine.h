#pragma once

#include "clock_engine.h"
#include "midi_message.h"
#include "sync.h"

/*! Effect slots, in their fixed processing order */
enum class FxSlot : uint8_t
{
  Transpose,
  Scale,
  Chord,
  Arp,
  Velocity,
  Cc,
  Count,
};

enum class FxScaleDir : uint8_t { Nearest, Up, Down, Count };
enum class FxChord : uint8_t { Major, Minor, Dom7, Min7, Sus4, Power, Octave, Diatonic, Count };
enum class FxArpRate : uint8_t { R4, R8, R16, R32, Count };
enum class FxArpMode : uint8_t { Up, Down, UpDown, Random, Played, Count };
enum class FxVelCurve : uint8_t { Linear, Soft, Hard, Fixed, Count };

/*! One input's effect chain. Each slot is on or off (bit n of `enabled` = FxSlot n). */
struct FxChainConfig
{
  uint8_t enabled = 0;
  bool bypass = false;
  int8_t transpose = 0; ///< -24..+24 semitones
  uint8_t scaleRoot = 0; ///< pitch class
  uint8_t scaleId = 0;   ///< MusicTheory::ScaleId (0 = chromatic: no default scale)
  FxScaleDir scaleDir = FxScaleDir::Nearest;
  FxChord chord = FxChord::Major;
  uint8_t chordInversion = 0; ///< 0..3
  bool chordOpen = false;     ///< spread: the middle note an octave up
  FxArpRate arpRate = FxArpRate::R16;
  FxArpMode arpMode = FxArpMode::Up;
  uint8_t arpOctaves = 1; ///< 1..4
  uint8_t arpGate = 50;   ///< percent of the arp step, 10..100
  bool arpLatch = false;
  FxVelCurve velCurve = FxVelCurve::Linear;
  uint8_t velMin = 1, velMax = 127, velFixed = 100, velRandom = 0;
  uint8_t ccSource = 74, ccTarget = 74, ccMin = 0, ccMax = 127; ///< one CC mapping
  bool ccInvert = false;
  uint8_t ccSmooth = 0; ///< slew time in 10 ms units (0 = off), up to 50 = 500 ms

  bool on(FxSlot slot) const { return !bypass && (enabled & (1 << (int)slot)); }
};

/*! Settings, persisted in NVS (key "fx"). A preset holds both inputs' chains. */
struct FxConfig
{
  static constexpr uint8_t kVersion = 1;
  static constexpr int kPresets = 8;

  uint8_t version = kVersion;
  uint8_t preset = 0; ///< the preset last loaded or saved
  FxChainConfig chains[2]; ///< per input (MidiSource)
  FxChainConfig presets[kPresets][2];
};

/*! MIDI effects on the merge path (stage 11): transpose, scale quantise, chord, arpeggiator,
 *  velocity curve, CC scaling/smoothing, one chain per input. The router calls process() after
 *  its filters and forced channel; everything the chain produces goes back through the
 *  router's destinations with the input's origin, so note tracking, panic and routing changes
 *  work as for plain thru. Each input note remembers the notes it produced, so its note-off
 *  releases exactly those even if the settings changed in between.
 *  UI side: config()/changed()/poll(), heldCount(). MIDI task side: everything else. */
class FxEngine
{
public:
  static constexpr int kMaxOut = 6;   ///< notes one input note can produce (chords)
  static constexpr int kSlots = 128;  ///< remembered input notes per input
  static constexpr int kArpNotes = 24;
  static constexpr int kPendingOffs = 16;
  static constexpr uint32_t kSaveDelayMillis = 2000;

  /*! Sends a produced message to the input's destinations (the router) */
  using Emit = void (*)(MidiSource input, const MidiMessage &msg, void *ctx);

  void begin(ClockEngine &clock); ///< before the MIDI task starts
  void setEmitter(Emit emit, void *ctx);

  // UI side
  FxConfig &config() { return _edit; }
  void changed();
  void poll(); ///< delayed save (only while the transport is stopped)
  void loadPreset(int preset); ///< copies the preset into both chains (call changed() after)
  void savePreset(int preset); ///< stores both chains in the preset (call changed() after)
  uint8_t heldCount(MidiSource input) const { return _held[(int)input]; }

  // MIDI task side
  /*! True if `input` needs the effect path: a slot is on, or notes produced earlier still need
   *  their note-offs. Otherwise the router sends the message unchanged. */
  bool engaged(MidiSource input);
  /*! Process a channel message (already filtered and channel-rewritten) */
  void process(const MidiMessage &msg);
  void run(); ///< loop hook: settings handoff, gate note-offs, 1/32 offbeats, CC smoothing
  /*! Forget everything for an input without sending (its notes were just released elsewhere) */
  void reset(MidiSource input);
  static void onClock(const ClockEvent &event, void *ctx);

  static uint8_t quantiseNote(uint8_t note, uint8_t root, uint8_t scaleId, FxScaleDir dir);
  static int chordOffsets(const FxChainConfig &c, uint8_t root, int8_t *out); ///< semitones from root
  static uint8_t curveVelocity(const FxChainConfig &c, uint8_t velocity, uint32_t random);
  static uint8_t mapCc(const FxChainConfig &c, uint8_t value);

private:
  struct Mapping
  {
    uint8_t channel = 0xFF; ///< 0xFF = free
    uint8_t inNote = 0;
    uint8_t count = 0;
    bool arp = false; ///< the notes went to the arpeggiator instead of straight out
    uint8_t out[kMaxOut];
  };
  struct ArpNote
  {
    uint8_t note = 0, velocity = 0, channel = 0, order = 0;
  };
  struct PendingOff
  {
    bool used = false;
    uint8_t channel = 0, note = 0;
    uint64_t dueUs = 0;
  };
  struct ChainState
  {
    Mapping map[kSlots];
    int mapped = 0;
    ArpNote arp[kArpNotes];
    int arpCount = 0;
    int keysDown = 0; ///< physical keys held that feed the arp (for latch)
    uint8_t arpOrder = 0;
    int arpIndex = 0;
    int arpDirection = 1;
    int stepCount = 0;
    PendingOff offs[kPendingOffs];
    bool secondDue = false; ///< 1/32: the offbeat note is due at secondUs
    uint64_t secondUs = 0;
    int ccValue = -1, ccTarget = -1; ///< smoothing state
    uint8_t ccChannel = 0;
    uint64_t ccNextUs = 0;
  };

  void applyPending();
  void emitNote(int input, uint8_t status, uint8_t channel, uint8_t note, uint8_t velocity);
  void noteOn(int input, const MidiMessage &msg);
  void noteOff(int input, const MidiMessage &msg);
  void controlChange(int input, const MidiMessage &msg);
  void arpStep(int input, uint64_t timeUs);
  void arpPlay(int input, uint64_t timeUs, uint32_t lengthUs);
  void arpReleaseSounding(int input);
  void arpClear(int input);
  Mapping *findMapping(ChainState &s, uint8_t channel, uint8_t note);
  uint32_t random();
  void load();
  void save();

  ClockEngine *_clock = nullptr;
  Emit _emit = nullptr;
  void *_emitCtx = nullptr;

  // UI side
  FxConfig _edit;
  uint32_t _changedMillis = 0;
  bool _dirty = false;

  // Handoff (guarded by _lock)
  CriticalSection _lock;
  FxChainConfig _pending[2];
  bool _pendingSet = false;

  // MIDI task side
  FxChainConfig _active[2];
  ChainState _state[2];
  bool _running = false;
  uint64_t _lastStepUs = 0;
  uint32_t _stepUs = 125000; ///< measured between Step events (120 BPM until known)
  uint32_t _rng = 0x13579BDF;
  volatile uint8_t _held[2] = {0, 0};
};
