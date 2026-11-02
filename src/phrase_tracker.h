#pragma once

#include <stdint.h>

#include "clock_engine.h"
#include "sync.h"

/*! Phrase settings, persisted in NVS (key "phrase"). Bump kVersion when the layout changes.
 *  Defaults are the stage 22 plan's "assumed until answered" values. */
struct PhraseConfig
{
  static constexpr uint8_t kVersion = 1;

  uint8_t version = kVersion;
  uint8_t phraseBars = 16;    ///< 4, 8, 16, 32
  uint8_t sectionPhrases = 4; ///< 0 = off, 2, 4, 8
  uint8_t warnBars = 1;       ///< bars before the phrase end that warn; 0 = off, 1, 2, 4
  uint8_t leds = 1;           ///< 0 off, 1 while the Phrase page is open, 2 always
  bool sppRealigns = true;    ///< Song Position sets bar 1 of the song = bar 1 of a phrase
};

/*! What the UI shows; copied under a lock */
struct PhraseSnapshot
{
  bool running;         ///< the transport is playing (Start/Continue seen)
  bool counting;        ///< bars are advancing (running, or free-running on a master's clock)
  bool synced;          ///< the position is known (Start, Song Position or a Bar 1 tap)
  int32_t bar;          ///< bar number with the offset applied (0 = bar 1 of a phrase)
  uint8_t barInPhrase;  ///< 0-based
  uint8_t phraseBars;
  uint16_t phrase;      ///< phrase number since bar 0
  uint8_t phraseInSection;
  uint8_t sectionPhrases; ///< 0 = no sections
  uint8_t barsLeft;     ///< including the current bar
  bool warning;         ///< inside the warning bars
  uint8_t beatInBar;    ///< 0-based
  uint8_t beatsPerBar;
  uint32_t phraseStarts; ///< increments on every phrase start (UI flashes on change)
  uint32_t nextPhraseTick; ///< tracker tick at which the next phrase starts (stage 13/15 hook)
};

/*! Keeps track of 4–32 bar phrases and their sections (stage 22).
 *
 *  Counting runs in the MIDI task from ClockEngine events: Start resets to bar 1, Song Position
 *  realigns (optional), and RawTick advances the count, including on a master's clock that was
 *  already running when OxiMate was plugged in (then the position shows as not synced until a
 *  Bar 1 tap). Everything is derived from one tick counter and a bar offset, so changing the
 *  phrase length takes effect immediately and consistently.
 *
 *  UI side: config()/changed()/poll(), realign()/reset(), snapshot().
 *  MIDI task side: onClock() (registered as a ClockEngine listener), run(). */
class PhraseTracker
{
public:
  /*! Called in the MIDI task when a phrase starts (stage 13 looper / stage 15 scenes) */
  using BoundaryListener = void (*)(uint16_t phrase, uint64_t timeUs, void *ctx);
  static constexpr int kMaxListeners = 4;

  void begin(ClockEngine &clock);

  // UI side
  PhraseConfig &config() { return _edit; }
  void changed(); ///< hand config() to the MIDI task; saved after a short delay while stopped
  void poll();    ///< delayed save
  /*! Bar 1 tap at `tapUs` (nowMicros64()): the nearest bar line becomes bar 1. With
   *  `phraseOnly`, the phrase restarts but the section count is kept. */
  void realign(uint64_t tapUs, bool phraseOnly = false);
  void reset(); ///< the current bar becomes bar 1 now
  PhraseSnapshot snapshot() const;

  // MIDI task side
  static void onClock(const ClockEvent &event, void *ctx);
  void handle(const ClockEvent &event, uint8_t beatsPerBar, double periodUs);
  void run(); ///< apply settings and UI commands
  void addBoundaryListener(BoundaryListener listener, void *ctx);
  uint32_t ticks() const { return _ticks; }

private:
  enum class Command : uint8_t
  {
    None,
    Realign,
    RealignPhrase,
    Reset,
  };

  static int32_t floorDiv(int64_t a, int32_t b) { return (int32_t)(a >= 0 ? a / b : -((-a + b - 1) / b)); }
  static int32_t floorMod(int32_t a, int32_t b) { return ((a % b) + b) % b; }

  void countTick(uint64_t timeUs);
  void doRealign(uint64_t tapUs, bool phraseOnly);
  void publish();
  void load();
  void save();

  ClockEngine *_clock = nullptr;

  // UI side
  PhraseConfig _edit;
  uint32_t _changedMillis = 0;
  bool _dirty = false;

  // Handoff (guarded by _lock)
  mutable CriticalSection _lock;
  PhraseConfig _pending;
  bool _pendingSet = false;
  Command _command = Command::None;
  uint64_t _commandUs = 0;
  PhraseSnapshot _snapshot = {};

  // MIDI task side
  PhraseConfig _active;
  uint8_t _beatsPerBar = 4;
  double _periodUs = 20833.3;
  bool _running = false;
  bool _synced = false;
  bool _seenTick = false;   ///< RawTicks arrive (internal clock or a master)
  bool _external = false;   ///< following a master's clock
  bool _stopped = false;    ///< the master sent Stop: its ticks no longer move the position
  uint32_t _ticks = 0;      ///< index of the next tick to count
  uint64_t _lastTickUs = 0; ///< when the last counted tick happened
  int32_t _offsetBars = 0;
  uint32_t _phraseStarts = 0;

  BoundaryListener _listeners[kMaxListeners];
  void *_listenerCtx[kMaxListeners];
  int _listenerCount = 0;
};
