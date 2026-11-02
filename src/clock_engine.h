#pragma once

#include "midi_message.h"
#include "sync.h"

class MidiOutputs;

enum class ClockMode : uint8_t
{
  Internal = 0,
  FollowUsb = 1,
  FollowDin = 2,
};

enum class ClockOutput : uint8_t
{
  Din = 0,
  Usb = 1,
  Count = 2, // stage 20 adds a gate output
};

struct ClockOutputConfig
{
  bool enabled = true;   ///< send F8 ticks
  bool transport = true; ///< send FA / FB / FC / F2
  int8_t ratio = 1;      ///< 1 = x1, 2..4 = multiply, -2..-4 = divide
};

/*! Clock settings, persisted in NVS (key "clock"). Bump kVersion when the layout changes. */
struct ClockConfig
{
  static constexpr uint8_t kVersion = 1;

  uint8_t version = kVersion;
  ClockMode mode = ClockMode::Internal;
  float bpm = 120.0f;     ///< 20.0 .. 300.0
  uint8_t swing = 50;     ///< 50 = straight .. 75
  uint8_t swingGrid = 16; ///< swing 16ths or 8ths
  uint8_t beatsPerBar = 4;
  bool imuTap = false;    ///< tap the case for tap tempo
  // Clock goes to USB by default and can be switched to DIN (decided 2026-11-01)
  ClockOutputConfig outputs[(int)ClockOutput::Count] = {{false, false, 1}, {true, true, 1}};
};

struct ClockEvent
{
  enum Type : uint8_t
  {
    Tick,
    Step,
    Beat,
    Bar,
    Start,
    Stop,
    Continue,
    Locate,
    RawTick, ///< every clock tick, running or not (stage 22: counting a master that was already playing)
  };
  Type type;
  uint32_t tick;   ///< 24 PPQN ticks since start (or the located position)
  uint32_t step;   ///< 16th-note index (Step events; swing applied to their timing)
  uint64_t timeUs; ///< when it was due
};

/*! The single source of outgoing MIDI clock and transport.
 *  Internal clock or following the USB or DIN input, with swing for OxiMate's own
 *  sequencers (Step events), per-output dividers/multipliers and a sub-tick scheduler.
 *  UI side: config()/changed()/poll(), start()/stop()/cont(), snapshot().
 *  MIDI task side: onExternal(), runDue(), schedule(), addListener(). */
class ClockEngine
{
public:
  using Listener = void (*)(const ClockEvent &event, void *ctx);
  using Callback = void (*)(void *ctx, uint32_t arg, uint64_t timeUs);

  static constexpr float kMinBpm = 20.0f;
  static constexpr float kMaxBpm = 300.0f;
  static constexpr int kMaxListeners = 8;
  static constexpr int kMaxScheduled = 32;
  static constexpr int kLockTicks = 24;

  struct Snapshot
  {
    ClockMode mode;
    float bpm; ///< set (internal) or measured (following)
    bool running;
    bool locked; ///< following: 24 consistent ticks seen
    bool lost;   ///< following: the external clock stopped arriving
    uint32_t tick;
    uint16_t bar;
    uint8_t beat; ///< 0-based beat within the bar
    uint8_t beatsPerBar;
  };

  void begin(MidiOutputs &outputs);

  // UI side
  ClockConfig &config() { return _edit; }
  void changed(); ///< apply config() to the MIDI task; saved after a short delay
  void poll();    ///< delayed save
  void start();
  void stop();
  void cont();
  void locate(uint16_t sixteenths);
  Snapshot snapshot() const;
  /*! Settings migrated from an older router version; applied unless a clock config was saved */
  void migrateFromRouter(ClockMode mode, bool clockOff);

  // MIDI task side
  /*! Clock and transport from an input (passed on by the router). Returns true if the
   *  message came from the followed input and was used. */
  bool onExternal(const MidiMessage &msg);
  void runDue(uint64_t nowUs);
  uint64_t nextDueUs() const; ///< earliest pending work, for arming the timer
  bool schedule(uint64_t atUs, Callback callback, void *ctx, uint32_t arg);
  void addListener(Listener listener, void *ctx);

private:
  enum class Command : uint8_t
  {
    Start,
    Stop,
    Continue,
    Locate,
  };
  struct Scheduled
  {
    uint64_t timeUs;
    Callback callback;
    void *ctx;
    uint32_t arg;
  };

  void pushCommand(Command command, uint16_t arg = 0);
  void runCommands(uint64_t nowUs);
  void applyPending(uint64_t nowUs);
  double periodUs() const; ///< current tick period: set tempo or measured
  void tick(uint64_t timeUs);
  void sendTick(int output);
  void sendTransport(uint8_t status);
  void doStart(uint64_t nowUs, bool sendMidi);
  void doStop(uint64_t nowUs, bool sendMidi);
  void doContinue(uint64_t nowUs, bool sendMidi);
  void emit(ClockEvent::Type type, uint64_t timeUs, uint32_t step = 0);
  void popScheduled(Scheduled &out);
  uint32_t swingDelayTicksX1000(uint32_t step) const; ///< swing delay of a step in 1/1000 ticks
  bool following(MidiSource source) const;
  void load();
  void save();

  static void stepCallback(void *ctx, uint32_t step, uint64_t timeUs);
  static void extraTickCallback(void *ctx, uint32_t output, uint64_t timeUs);

  MidiOutputs *_outputs = nullptr;

  // UI side
  ClockConfig _edit;
  uint32_t _changedMillis = 0;
  bool _dirty = false;
  bool _savedConfig = false;

  // Handoff (guarded by _lock)
  mutable CriticalSection _lock;
  ClockConfig _pending;
  bool _pendingSet = false;
  static constexpr int kCommands = 8;
  Command _commands[kCommands];
  uint16_t _commandArgs[kCommands];
  int _commandCount = 0;
  Snapshot _snapshot = {};

  // MIDI task side
  ClockConfig _active;
  bool _running = false;
  uint32_t _tick = 0;    ///< position in 24 PPQN ticks
  uint32_t _outTicks[(int)ClockOutput::Count] = {0, 0};
  double _nextTickUs = 0; ///< internal clock: when the next tick is due (no cumulative drift)

  // Following an external clock
  uint32_t _lastExtMicros = 0;
  uint64_t _lastExtUs = 0;
  bool _haveExt = false;
  double _extPeriodUs = 0;
  int _consistent = 0;
  bool _locked = false;
  bool _lost = false;

  Scheduled _scheduled[kMaxScheduled]; // binary min-heap on timeUs
  int _scheduledCount = 0;

  Listener _listeners[kMaxListeners];
  void *_listenerCtx[kMaxListeners];
  int _listenerCount = 0;
};
