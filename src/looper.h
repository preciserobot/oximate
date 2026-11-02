#pragma once

#include <atomic>
#include "clock_engine.h"
#include "loop_buffer.h"
#include "midi_outputs.h"
#include "sync.h"

class PhraseTracker;

/*! Looper settings, persisted in NVS (key "loop"). Bump kVersion when the layout changes. */
struct LoopConfig
{
  static constexpr uint8_t kVersion = 1;

  uint8_t version = kVersion;
  uint8_t bars = 4;           ///< 1, 2, 4, 8 or 16
  uint8_t quantise = 0;       ///< 0 off, 1 = 1/16, 2 = 1/8, 3 = 1/16 triplets
  uint8_t inputs = 3;         ///< bit 0 USB, bit 1 DIN
  uint8_t channel = 0;        ///< 0 = all, 1-16
  uint8_t outputs = 1;        ///< bit 0 DIN, bit 1 USB
  bool startOnPhrase = false; ///< recording starts at the next phrase (stage 22) instead of the next bar
};

enum class LoopState : uint8_t
{
  Empty,     ///< nothing recorded
  Armed,     ///< waiting for the next bar (or phrase) to start recording
  Recording, ///< first pass
  Looping,   ///< has a loop (playing while the clock runs and playback is on)
};

/*! What the Loop page shows (copied under a lock) */
struct LoopSnapshot
{
  LoopState state;
  bool active;   ///< the loop is running with the clock
  bool playing;  ///< playback on (B)
  bool overdub;  ///< recording a pass on top
  uint8_t bars;
  uint8_t passes;
  uint32_t events;
  uint32_t position; ///< 24 PPQN ticks into the loop
  uint32_t length;   ///< loop length in 24 PPQN ticks (0 = none)
  uint8_t rowChannel[4]; ///< overview: the busiest channels (1-16, 0 = unused row)
  uint64_t rowBits[4];   ///< note-ons in 64 columns across the loop
  uint32_t changes;      ///< increments when the content changed
};

/*! Clock-synced MIDI looper (stage 13).
 *
 *  Records routed input (an InputHooks observer: only forwarded messages, so echoes aren't
 *  recorded twice) for 1-16 bars from the next bar line, then loops it with the clock engine,
 *  sending with Origin::internal(13) to DIN and/or USB. Overdub records passes on top; undo
 *  removes the last pass. Notes held across the loop end are cut there and restarted at the
 *  start. Stop, clear and undo release everything still sounding.
 *
 *  UI side: config()/changed()/poll(), the commands, snapshot(), export/import for files.
 *  MIDI task side: onClock (clock listener), observer, onPhrase, run(). */
class Looper
{
public:
  static constexpr uint8_t kOwner = 13;
  static constexpr uint32_t kCapacity = 65536;       ///< events (512 KB of PSRAM)
  static constexpr uint32_t kPendingCapacity = 4096; ///< events of one pass and cycle
  static constexpr uint32_t kSaveDelayMillis = 2000;

  /*! Allocates the buffers (PSRAM on the device) and registers the clock listener, the input
   *  observer and (if given) the phrase boundary listener. Before the MIDI task starts. */
  bool begin(MidiOutputs &outputs, ClockEngine &clock, PhraseTracker *phrase = nullptr,
             uint32_t capacity = kCapacity);

  // UI side
  LoopConfig &config() { return _edit; }
  void changed(); ///< apply config(); saved after a short delay
  void poll();
  void rec();           ///< A: arm (empty), disarm (armed), cancel the first pass (recording)
  void undo();          ///< hold A: remove the last pass
  void clear();
  void togglePlay();    ///< B: playback on / off
  void toggleOverdub(); ///< C: record a new pass on top while looping
  LoopSnapshot snapshot() const;

  /*! Files: ask for a copy of the loop (done by the MIDI task within ~1 ms); when ready, the
   *  copy stays valid until the next request or import */
  void requestExport();
  bool exportReady() const { return _exportReady; }
  const LoopEvent *transfer() const { return _transfer; }
  LoopEvent *transferBuffer() { return _transfer; }
  uint32_t transferCount() const { return _transferCount; }
  uint32_t transferLength() const { return _transferLength; } ///< 96 PPQN
  uint32_t capacity() const { return _capacity; }
  /*! Replace the loop with `count` events from transferBuffer() (sorted, 96 PPQN) */
  void importTransfer(uint32_t count, uint32_t length96);

  // MIDI task side
  void run(uint32_t nowMillis);
  static void onClock(const ClockEvent &event, void *ctx);
  static void observer(const MidiMessage &msg, bool forwarded, void *ctx);
  static void onPhrase(uint16_t phrase, uint64_t timeUs, void *ctx);

  static uint32_t quantiseTicks(uint8_t setting); ///< at 96 PPQN

private:
  enum Command : uint8_t
  {
    CmdRec = 1,
    CmdUndo = 2,
    CmdClear = 4,
    CmdPlay = 8,
    CmdOverdub = 16,
  };

  void command(uint8_t cmd);
  void handle(const ClockEvent &event);
  void startRecording();
  void wrap();
  void playWindow(uint32_t position, uint64_t tickUs);
  void send(uint8_t status, uint8_t data1, uint8_t data2);
  void releaseAll();
  void stopRunning();
  void publish();
  void load();
  void save();
  static void scheduledCallback(void *ctx, uint32_t packed, uint64_t timeUs);

  MidiOutputs *_outputs = nullptr;
  ClockEngine *_clock = nullptr;
  LoopBuffer _buffer;
  LoopEvent *_transfer = nullptr;
  uint32_t _capacity = 0;

  // UI side
  LoopConfig _edit;
  bool _dirty = false;
  uint32_t _changedMillis = 0;

  // Handoff (guarded by _lock)
  mutable CriticalSection _lock;
  LoopConfig _pending;
  bool _pendingSet = false;
  uint8_t _commands = 0;
  bool _importPending = false;
  uint32_t _importCount = 0, _importLength = 0;
  LoopSnapshot _snapshot = {};

  // Transfer (export) state
  std::atomic<bool> _exportRequested{false};
  std::atomic<bool> _exportReady{false};
  uint32_t _transferCount = 0, _transferLength = 0;

  // MIDI task side
  LoopConfig _active;
  LoopState _state = LoopState::Empty;
  bool _running = false;   ///< the clock transport is running
  bool _loopActive = false; ///< positions advance (recording or playing)
  bool _waitBar = false;    ///< start the loop at the next bar line
  bool _phraseNow = false;  ///< a phrase boundary is on this bar
  bool _playing = true;
  bool _overdub = false;
  uint8_t _pass = 0;
  uint32_t _position = 0; ///< 24 PPQN ticks into the loop
  uint32_t _length = 0;   ///< 24 PPQN
  uint64_t _lastTickUs = 0;
  double _periodUs = 20833.3;
  uint32_t _changes = 0;
  bool _overviewDirty = true;
  uint8_t _rowChannel[4] = {};
  uint64_t _rowBits[4] = {};
  uint32_t _lastPublish = 0;
};
