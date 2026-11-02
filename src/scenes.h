#pragma once

#include <stdint.h>

#include "clock_engine.h"
#include "midi_outputs.h"
#include "router.h"
#include "sam_synth.h"
#include "sync.h"

class DrumMachine;
class FxEngine;
class GenEngine;
class PhraseTracker;

/*! A device a scene can talk to (set up once on the Devices view) */
struct SceneDevice
{
  uint8_t name;          ///< index into Scenes::kDeviceNames
  uint8_t output;        ///< 0 = DIN, 1 = USB
  uint8_t channel;       ///< 1-16; 0 = slot unused
  bool pcAllowed;        ///< program changes may be sent (off by default: Elektrons switch patterns)
  bool pcOneBased;       ///< show PCs as 1-128 (else 0-127), like the device's display
  uint8_t leadBeats;     ///< PCs go out this many beats before the boundary (0-4)
};

struct SceneCc
{
  uint8_t cc;
  uint8_t value;
};

/*! What one scene sends to one device */
struct SceneDeviceMessages
{
  static constexpr int kMaxCcs = 16;
  int8_t program; ///< -1 = none, 0-127
  int8_t bankMsb; ///< -1 = none
  int8_t bankLsb; ///< -1 = none
  uint8_t ccCount;
  SceneCc ccs[kMaxCcs];
};

enum class SceneTiming : uint8_t
{
  Now = 0,
  NextBar = 1,
  NextPhrase = 2, ///< stage 22; falls back to the next bar while phrases aren't counting
};

/*! One scene of the setlist. "Optional" parts are -1 / 0 when unset and are then left alone. */
struct Scene
{
  static constexpr int kDevices = 8;
  uint8_t name;           ///< index into Scenes::kSceneNames
  SceneTiming timing;
  float tempo;            ///< 0 = unset; ignored while following an external clock
  int8_t snapshot;        ///< OxiMate snapshot slot (router + synth), -1 = none
  int8_t drumPattern;     ///< -1 = none
  int8_t fxPreset;        ///< -1 = none
  bool genSet;            ///< genActive applies
  uint8_t genActive;      ///< bit n = Gen slot n unmuted
  SceneDeviceMessages devices[kDevices];
};

/*! Router and synth settings captured by "Capture now", shared by scenes through a slot */
struct SceneSnapshot
{
  bool used;
  RouterConfig router;
  SynthConfig synth;
};

/*! Pointers to the parts a scene can recall (any may be null) */
struct SceneTargets
{
  ClockEngine *clock = nullptr;
  Router *router = nullptr;
  SamSynth *synth = nullptr;
  DrumMachine *drums = nullptr;
  FxEngine *fx = nullptr;
  GenEngine *gen = nullptr;
  PhraseTracker *phrase = nullptr;
};

/*! What the page shows of the recall engine (copied under the lock) */
struct SceneStatus
{
  bool pending;      ///< a recall waits for its boundary
  int8_t scene;      ///< the pending (or last recalled) scene
  SceneTiming timing;
  int32_t ticksLeft; ///< clock ticks to the boundary while pending, -1 = unknown
  uint32_t fired;    ///< recalls done so far (the UI applies OxiMate settings on change)
  uint8_t pacedLeft; ///< CC messages still being spread out
};

/*! Setlist and scenes (stage 15): one touch sends program changes and CC snapshots to your gear
 *  and recalls OxiMate's own settings, now, on the next bar or on the next phrase.
 *
 *  UI side: setlist editing (devices(), scene(), add/remove), recall(), cancel(), capture(),
 *  learn mode, poll() (applies OxiMate settings after a recall, delayed saves).
 *  MIDI task side: onClock() (ClockEngine listener), onPhrase() (PhraseTracker boundary),
 *  observeInput() (Learn CCs), run() (recall handoff, PC lead times, CC spreading).
 *  Device messages go out through MidiOutputs with Origin::internal(15). */
class Scenes
{
public:
  static constexpr uint8_t kOwner = 15;
  static constexpr int kMaxScenes = 32;
  static constexpr int kSnapshots = 8;
  static constexpr int kDevices = Scene::kDevices;
  static constexpr int kPacePerChunk = 4;           ///< CC messages per chunk (~4 ms on DIN)
  static constexpr uint32_t kPaceMillis = 10;       ///< one chunk every 10 ms
  static constexpr uint32_t kSaveDelayMillis = 2000;
  static const char *const kDeviceNames[];
  static const int kDeviceNameCount;
  static const char *const kSceneNames[];
  static const int kSceneNameCount;

  void begin(MidiOutputs &outputs, const SceneTargets &targets); ///< before the MIDI task starts

  // UI side: the setlist
  SceneDevice &device(int index) { return _devices[index]; }
  Scene &scene(int index) { return _scenes[index]; }
  int count() const { return _count; }
  int current() const { return _current; } ///< last recalled scene, -1 = none yet
  int armed() const { return _armed; }     ///< the scene B recalls
  void setArmed(int index);
  int addScene(int after);    ///< a new empty scene after `after`; returns its index, -1 when full
  void removeScene(int index); ///< the last remaining scene is kept (cleared)
  void changed(int scene = -1); ///< setlist edited (scene index, or -1 for devices); saved later
  bool setProgram(int sceneIndex, int device, int program); ///< false: PCs not allowed for it
  SceneSnapshot &snapshot(int slot) { return _snapshots[slot]; }

  /*! Store OxiMate's current tempo, router, synth, drum pattern, FX preset and Gen mutes in the
   *  scene (router and synth in a snapshot slot). False if no snapshot slot is free. */
  bool capture(int sceneIndex);
  void clearCapture(int sceneIndex);

  /*! Recall a scene with its timing (or now). The armed scene moves to the next one. */
  void recall(int sceneIndex, bool now = false);
  void cancel(); ///< drop a pending recall and unsent CCs (panic calls this)
  SceneStatus status() const;

  /*! Learn CCs: while on, CCs arriving on a device's channel are stored in that scene */
  void setLearn(int sceneIndex); ///< -1 = off
  int learning() const { return _learnScene; }

  void poll(uint32_t nowMillis); ///< UI loop: apply recalls' OxiMate settings, learned CCs, saving

  // MIDI task side
  static void onClock(const ClockEvent &event, void *ctx);
  static void onPhrase(uint16_t phrase, uint64_t timeUs, void *ctx);
  static void observeInput(const MidiMessage &msg, bool forwarded, void *ctx);
  void run(uint32_t nowMillis);

private:
  /*! What the UI hands to the MIDI task for one recall */
  struct Plan
  {
    int8_t scene;
    bool now;
    Scene content;
    SceneDevice devices[kDevices];
  };
  struct Paced
  {
    uint8_t output;
    uint8_t bytes[3];
  };
  struct Learned
  {
    uint8_t channel;
    uint8_t cc;
    uint8_t value;
  };

  // MIDI task
  bool running() const;
  void startPlan(const Plan &plan, uint32_t nowMillis);
  void sendLeadPrograms(bool all);
  void fire();
  void sendProgram(const SceneDevice &device, const SceneDeviceMessages &messages);
  void send(uint8_t output, const uint8_t *bytes, uint8_t length);
  void pace(uint32_t nowMillis);
  void publish();

  // UI
  void applyOxiMate(int sceneIndex);
  void clearScene(Scene &scene);
  void load();
  void save();

  MidiOutputs *_outputs = nullptr;
  SceneTargets _targets;

  // UI side state
  SceneDevice _devices[kDevices];
  Scene *_scenes = nullptr; ///< kMaxScenes, allocated in PSRAM by begin() (~10 KB: too big for DRAM)
  SceneSnapshot _snapshots[kSnapshots];
  int _count = 1;
  int _current = -1;
  int _armed = 0;
  int _requested = -1;     ///< scene of the last recall request
  uint32_t _appliedFired = 0;
  int _learnScene = -1;
  uint32_t _dirtyScenes = 0; ///< bit n = scene n needs saving
  bool _dirtySetlist = false;
  uint8_t _dirtySnapshots = 0;
  uint32_t _changedMillis = 0;

  // Handoff (guarded by _lock)
  mutable CriticalSection _lock;
  Plan _plan;
  bool _planSet = false;
  bool _cancel = false;
  bool _learnOn = false;
  SceneStatus _status = {false, -1, SceneTiming::Now, -1, 0, 0};
  static constexpr int kLearned = 32;
  Learned _learned[kLearned];
  int _learnedHead = 0, _learnedTail = 0;

  // MIDI task state
  Plan _active;
  bool _pending = false;
  bool _waitPhrase = false;
  bool _running = false;
  uint32_t _tick = 0;          ///< last clock tick seen (24 PPQN since Start)
  uint8_t _beatsPerBar = 4;
  int64_t _boundaryTick = -1;  ///< clock tick of the boundary, -1 = unknown
  bool _leadSent[kDevices];
  bool _fireNow = false;
  uint32_t _fired = 0;
  static constexpr int kPaced = kDevices * SceneDeviceMessages::kMaxCcs;
  Paced _paced[kPaced];
  int _pacedCount = 0, _pacedNext = 0;
  uint32_t _lastPaceMillis = 0;
};
