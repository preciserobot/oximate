#include "scenes.h"

#include <Arduino.h>
#include <Preferences.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(ESP_PLATFORM)
#include <esp_heap_caps.h>
#endif

#include "drum_machine.h"
#include "fx_engine.h"
#include "gen_engine.h"
#include "input_observers.h"
#include "phrase_tracker.h"

static const char *const kNamespace = "oximate";
static const char *const kSetlistKey = "scenes";
static constexpr uint8_t kVersion = 1;

const char *const Scenes::kDeviceNames[] = {"Digitakt", "Digitone", "Syntakt", "Rytm",  "Oxi One",
                                            "M8",       "DFAM",     "Synth",   "Device"};
const int Scenes::kDeviceNameCount = sizeof(kDeviceNames) / sizeof(kDeviceNames[0]);
const char *const Scenes::kSceneNames[] = {"Scene", "Intro", "Groove", "Build", "Break", "Drop",
                                           "Peak",  "Main",  "Fill",   "Outro"};
const int Scenes::kSceneNameCount = sizeof(kSceneNames) / sizeof(kSceneNames[0]);

/*! The stored setlist header (key "scenes"); scenes and snapshots have their own keys */
struct SetlistHeader
{
  uint8_t version;
  uint8_t count;
  SceneDevice devices[Scene::kDevices];
};

// ---------------------------------------------------------------------------- UI side

void Scenes::begin(MidiOutputs &outputs, const SceneTargets &targets)
{
  _outputs = &outputs;
  _targets = targets;
  if (!_scenes) {
#if defined(ESP_PLATFORM)
    _scenes = static_cast<Scene *>(heap_caps_malloc(sizeof(Scene) * kMaxScenes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!_scenes) _scenes = static_cast<Scene *>(malloc(sizeof(Scene) * kMaxScenes));
#else
    _scenes = new Scene[kMaxScenes];
#endif
  }
  for (int d = 0; d < kDevices; d++) _devices[d] = {(uint8_t)(kDeviceNameCount - 1), 0, 0, false, true, 0};
  for (int i = 0; i < kMaxScenes; i++) clearScene(_scenes[i]);
  for (SceneSnapshot &snapshot : _snapshots) snapshot.used = false;
  memset(&_active, 0, sizeof(_active));
  _active.scene = -1;
  load();
  if (_targets.clock) _targets.clock->addListener(onClock, this);
  if (_targets.phrase) _targets.phrase->addBoundaryListener(onPhrase, this);
  InputHooks::addObserver(observeInput, this);
}

void Scenes::clearScene(Scene &scene)
{
  memset(&scene, 0, sizeof(scene));
  scene.timing = SceneTiming::NextBar; // decided default
  scene.snapshot = -1;
  scene.drumPattern = -1;
  scene.fxPreset = -1;
  for (SceneDeviceMessages &messages : scene.devices) {
    messages.program = messages.bankMsb = messages.bankLsb = -1;
  }
}

void Scenes::setArmed(int index)
{
  if (index < 0) index = 0;
  if (index >= _count) index = _count - 1;
  _armed = index;
}

int Scenes::addScene(int after)
{
  if (_count >= kMaxScenes) return -1;
  int index = after + 1;
  if (index < 0) index = 0;
  if (index > _count) index = _count;
  for (int i = _count; i > index; i--) _scenes[i] = _scenes[i - 1];
  clearScene(_scenes[index]);
  _count++;
  if (_current >= index) _current++;
  if (_armed >= index && _armed < _count - 1) _armed++;
  _dirtyScenes |= ~0u << index; // everything from here on moved
  changed(-1);
  return index;
}

void Scenes::removeScene(int index)
{
  if (index < 0 || index >= _count) return;
  if (_scenes[index].snapshot >= 0) {
    _snapshots[_scenes[index].snapshot].used = false;
    _dirtySnapshots |= 1 << _scenes[index].snapshot;
  }
  for (int i = index; i < _count - 1; i++) _scenes[i] = _scenes[i + 1];
  if (_count > 1) {
    _count--;
  } else {
    clearScene(_scenes[0]);
  }
  if (_current == index) _current = -1;
  else if (_current > index) _current--;
  setArmed(_armed > index ? _armed - 1 : _armed);
  _dirtyScenes |= ~0u << index;
  changed(-1);
}

void Scenes::changed(int scene)
{
  if (scene >= 0) {
    _dirtyScenes |= 1u << scene;
  } else {
    _dirtySetlist = true;
  }
  _changedMillis = millis();
}

bool Scenes::setProgram(int sceneIndex, int device, int program)
{
  // Elektron safety: a scene can only hold a PC for a device that allows program changes
  if (program >= 0 && !_devices[device].pcAllowed) return false;
  _scenes[sceneIndex].devices[device].program = program < 0 ? -1 : (program > 127 ? 127 : program);
  changed(sceneIndex);
  return true;
}

bool Scenes::capture(int sceneIndex)
{
  Scene &scene = _scenes[sceneIndex];
  if (_targets.clock) {
    const ClockConfig &clock = _targets.clock->config();
    scene.tempo = clock.mode == ClockMode::Internal ? clock.bpm : 0;
  }
  if (_targets.drums) scene.drumPattern = _targets.drums->playingPattern();
  if (_targets.fx) scene.fxPreset = _targets.fx->config().preset;
  if (_targets.gen) {
    scene.genSet = true;
    scene.genActive = 0;
    for (int slot = 0; slot < GenEngine::kSlots; slot++) {
      if (!_targets.gen->config().slots[slot].muted) scene.genActive |= 1 << slot;
    }
  }
  int slot = scene.snapshot;
  if (slot < 0) {
    for (int i = 0; i < kSnapshots && slot < 0; i++) {
      if (!_snapshots[i].used) slot = i;
    }
  }
  bool ok = slot >= 0;
  if (ok) {
    SceneSnapshot &snapshot = _snapshots[slot];
    snapshot.used = true;
    if (_targets.router) snapshot.router = _targets.router->config();
    if (_targets.synth) snapshot.synth = _targets.synth->config();
    scene.snapshot = slot;
    _dirtySnapshots |= 1 << slot;
  }
  changed(sceneIndex);
  return ok;
}

void Scenes::clearCapture(int sceneIndex)
{
  Scene &scene = _scenes[sceneIndex];
  if (scene.snapshot >= 0) {
    _snapshots[scene.snapshot].used = false;
    _dirtySnapshots |= 1 << scene.snapshot;
  }
  scene.snapshot = -1;
  scene.tempo = 0;
  scene.drumPattern = -1;
  scene.fxPreset = -1;
  scene.genSet = false;
  changed(sceneIndex);
}

void Scenes::recall(int sceneIndex, bool now)
{
  if (sceneIndex < 0 || sceneIndex >= _count) return;
  Plan plan;
  plan.scene = sceneIndex;
  plan.now = now;
  plan.content = _scenes[sceneIndex];
  memcpy(plan.devices, _devices, sizeof(_devices));
  for (int d = 0; d < kDevices; d++) { // unused devices and PCs that aren't allowed never go out
    if (!plan.devices[d].channel) plan.content.devices[d].ccCount = 0;
    if (!plan.devices[d].channel || !plan.devices[d].pcAllowed) plan.content.devices[d].program = -1;
  }
  {
    CriticalGuard guard(_lock);
    _plan = plan;
    _planSet = true;
  }
  _requested = sceneIndex;
  setArmed(sceneIndex + 1 < _count ? sceneIndex + 1 : sceneIndex);
}

void Scenes::cancel()
{
  CriticalGuard guard(_lock);
  _cancel = true;
  _planSet = false;
}

SceneStatus Scenes::status() const
{
  CriticalGuard guard(_lock);
  return _status;
}

void Scenes::setLearn(int sceneIndex)
{
  _learnScene = sceneIndex;
  CriticalGuard guard(_lock);
  _learnOn = sceneIndex >= 0;
  _learnedHead = _learnedTail = 0;
}

void Scenes::applyOxiMate(int sceneIndex)
{
  if (sceneIndex < 0 || sceneIndex >= _count) return;
  const Scene &scene = _scenes[sceneIndex];
  if (scene.tempo > 0 && _targets.clock && _targets.clock->config().mode == ClockMode::Internal) {
    _targets.clock->config().bpm = scene.tempo;
    _targets.clock->changed();
  }
  if (scene.snapshot >= 0 && _snapshots[scene.snapshot].used) {
    const SceneSnapshot &snapshot = _snapshots[scene.snapshot];
    if (_targets.router) {
      // Through the router's own changed(): held notes are released before routing changes
      RouterConfig router = snapshot.router;
      router.ledLevel = _targets.router->config().ledLevel; // a display preference, not part of a scene
      _targets.router->config() = router;
      _targets.router->changed();
    }
    if (_targets.synth) {
      // SamSynth diffs the change and only sends what differs; the output choice stays global
      SynthConfig synth = snapshot.synth;
      const SynthConfig &now = _targets.synth->config();
      synth.speaker = now.speaker;
      synth.bypass = now.bypass;
      synth.configChannel = now.configChannel;
      _targets.synth->config() = synth;
      _targets.synth->changed();
    }
  }
  if (scene.drumPattern >= 0 && _targets.drums) _targets.drums->queuePattern(scene.drumPattern);
  if (scene.fxPreset >= 0 && _targets.fx) {
    _targets.fx->loadPreset(scene.fxPreset);
    _targets.fx->changed();
  }
  if (scene.genSet && _targets.gen) {
    for (int slot = 0; slot < GenEngine::kSlots; slot++) {
      _targets.gen->config().slots[slot].muted = !(scene.genActive & (1 << slot));
    }
    _targets.gen->changed();
  }
}

void Scenes::poll(uint32_t nowMillis)
{
  SceneStatus now = status();
  if (now.fired != _appliedFired) {
    _appliedFired = now.fired;
    applyOxiMate(now.scene);
    _current = now.scene;
  }

  // Learned CCs go into the scene being learned, for the device listening on that channel
  Learned learned[kLearned];
  int count = 0;
  {
    CriticalGuard guard(_lock);
    while (_learnedTail != _learnedHead && count < kLearned) {
      learned[count++] = _learned[_learnedTail];
      _learnedTail = (_learnedTail + 1) % kLearned;
    }
  }
  if (_learnScene >= 0 && _learnScene < _count) {
    for (int i = 0; i < count; i++) {
      for (int d = 0; d < kDevices; d++) {
        if (_devices[d].channel != learned[i].channel) continue;
        SceneDeviceMessages &messages = _scenes[_learnScene].devices[d];
        int slot = 0;
        while (slot < messages.ccCount && messages.ccs[slot].cc != learned[i].cc) slot++;
        if (slot < SceneDeviceMessages::kMaxCcs) {
          messages.ccs[slot] = {learned[i].cc, learned[i].value};
          if (slot == messages.ccCount) messages.ccCount++;
          changed(_learnScene);
        }
        break;
      }
    }
  }

  // Flash writes stall both cores: only while the transport is stopped
  bool dirty = _dirtySetlist || _dirtyScenes || _dirtySnapshots;
  bool running = _targets.clock && _targets.clock->snapshot().running;
  if (dirty && !running && nowMillis - _changedMillis >= kSaveDelayMillis) save();
}

void Scenes::load()
{
  Preferences prefs;
  if (!prefs.begin(kNamespace, true)) return;
  SetlistHeader header;
  if (prefs.getBytesLength(kSetlistKey) == sizeof(header) &&
      prefs.getBytes(kSetlistKey, &header, sizeof(header)) == sizeof(header) && header.version == kVersion &&
      header.count >= 1 && header.count <= kMaxScenes) {
    _count = header.count;
    memcpy(_devices, header.devices, sizeof(_devices));
    char key[8];
    for (int i = 0; i < _count; i++) {
      snprintf(key, sizeof(key), "scn%02d", i);
      Scene scene;
      if (prefs.getBytesLength(key) == sizeof(scene) && prefs.getBytes(key, &scene, sizeof(scene)) == sizeof(scene)) {
        _scenes[i] = scene;
      }
    }
    for (int i = 0; i < kSnapshots; i++) {
      snprintf(key, sizeof(key), "ssn%d", i);
      SceneSnapshot snapshot;
      if (prefs.getBytesLength(key) == sizeof(snapshot) &&
          prefs.getBytes(key, &snapshot, sizeof(snapshot)) == sizeof(snapshot)) {
        _snapshots[i] = snapshot;
      }
    }
  }
  prefs.end();
}

void Scenes::save()
{
  Preferences prefs;
  if (!prefs.begin(kNamespace, false)) return;
  char key[8];
  SetlistHeader header;
  header.version = kVersion;
  header.count = _count;
  memcpy(header.devices, _devices, sizeof(_devices));
  prefs.putBytes(kSetlistKey, &header, sizeof(header));
  for (int i = 0; i < kMaxScenes; i++) {
    if (!(_dirtyScenes & (1u << i))) continue;
    snprintf(key, sizeof(key), "scn%02d", i);
    if (i < _count) prefs.putBytes(key, &_scenes[i], sizeof(Scene));
    else prefs.remove(key);
  }
  for (int i = 0; i < kSnapshots; i++) {
    if (!(_dirtySnapshots & (1 << i))) continue;
    snprintf(key, sizeof(key), "ssn%d", i);
    prefs.putBytes(key, &_snapshots[i], sizeof(SceneSnapshot));
  }
  prefs.end();
  _dirtySetlist = false;
  _dirtyScenes = 0;
  _dirtySnapshots = 0;
}

// ---------------------------------------------------------------------------- MIDI task side

void Scenes::send(uint8_t output, const uint8_t *bytes, uint8_t length)
{
  MidiOutput out = output == 1 ? MidiOutput::Usb : MidiOutput::Din;
  if (!_outputs->available(out)) return;
  _outputs->send(out, {MidiSource::Din, 0, bytes, length}, Origin::internal(kOwner));
}

void Scenes::sendProgram(const SceneDevice &device, const SceneDeviceMessages &messages)
{
  uint8_t channel = (device.channel - 1) & 0x0F;
  if (messages.bankMsb >= 0) {
    uint8_t bank[3] = {(uint8_t)(0xB0 | channel), 0, (uint8_t)messages.bankMsb};
    send(device.output, bank, 3);
  }
  if (messages.bankLsb >= 0) {
    uint8_t bank[3] = {(uint8_t)(0xB0 | channel), 32, (uint8_t)messages.bankLsb};
    send(device.output, bank, 3);
  }
  uint8_t program[2] = {(uint8_t)(0xC0 | channel), (uint8_t)messages.program};
  send(device.output, program, 2);
}

/*! PCs with a lead time go out that many beats before the boundary (all of them with `all`) */
void Scenes::sendLeadPrograms(bool all)
{
  for (int d = 0; d < kDevices; d++) {
    const SceneDevice &device = _active.devices[d];
    const SceneDeviceMessages &messages = _active.content.devices[d];
    if (_leadSent[d] || messages.program < 0 || !device.leadBeats) continue;
    bool due = all || _boundaryTick < 0 || _boundaryTick - (int64_t)_tick <= device.leadBeats * 24;
    if (due) {
      sendProgram(device, messages);
      _leadSent[d] = true;
    }
  }
}

bool Scenes::running() const
{
  // The clock engine's own view (also right when OxiMate started while a master was playing)
  return _targets.clock ? _targets.clock->snapshot().running : _running;
}

void Scenes::startPlan(const Plan &plan, uint32_t nowMillis)
{
  _active = plan;
  _pending = true;
  _fireNow = false;
  _waitPhrase = false;
  _boundaryTick = -1;
  for (bool &sent : _leadSent) sent = false;
  if (_targets.clock) {
    uint8_t beats = _targets.clock->snapshot().beatsPerBar;
    _beatsPerBar = beats ? beats : 4;
  }
  SceneTiming timing = plan.content.timing;
  if (plan.now || timing == SceneTiming::Now || !running()) {
    _fireNow = true; // stopped: quantised recalls happen immediately
    return;
  }
  if (timing == SceneTiming::NextPhrase && _targets.phrase) {
    PhraseSnapshot phrase = _targets.phrase->snapshot();
    if (phrase.synced && phrase.counting) {
      _waitPhrase = true;
      _boundaryTick = (int64_t)_tick + (int64_t)phrase.nextPhraseTick - (int64_t)_targets.phrase->ticks();
    }
  }
  if (!_waitPhrase) { // next bar (also the fallback while phrases aren't counting)
    uint32_t barTicks = 24u * _beatsPerBar;
    _boundaryTick = ((int64_t)_tick / barTicks + 1) * barTicks;
  }
  sendLeadPrograms(false); // already inside a lead time: send now
  (void)nowMillis;
}

void Scenes::fire()
{
  // 1. remaining PCs (with lead time: already sent), 2. OxiMate settings (UI, on `fired`),
  // 3. CC snapshots, spread out in chunks
  for (int d = 0; d < kDevices; d++) {
    const SceneDeviceMessages &messages = _active.content.devices[d];
    if (messages.program >= 0 && !_leadSent[d]) sendProgram(_active.devices[d], messages);
  }
  _fired++;
  _pacedCount = _pacedNext = 0;
  for (int d = 0; d < kDevices; d++) {
    const SceneDevice &device = _active.devices[d];
    const SceneDeviceMessages &messages = _active.content.devices[d];
    for (int i = 0; i < messages.ccCount && _pacedCount < kPaced; i++) {
      _paced[_pacedCount++] = {device.output,
                               {(uint8_t)(0xB0 | ((device.channel - 1) & 0x0F)), messages.ccs[i].cc,
                                messages.ccs[i].value}};
    }
  }
  _lastPaceMillis = 0;
  _pending = false;
  _fireNow = false;
}

void Scenes::pace(uint32_t nowMillis)
{
  if (_pacedNext >= _pacedCount) return;
  if (_lastPaceMillis && nowMillis - _lastPaceMillis < kPaceMillis) return;
  _lastPaceMillis = nowMillis ? nowMillis : 1;
  for (int i = 0; i < kPacePerChunk && _pacedNext < _pacedCount; i++) {
    const Paced &paced = _paced[_pacedNext];
    MidiOutput out = paced.output == 1 ? MidiOutput::Usb : MidiOutput::Din;
    if (_outputs->available(out) && !_outputs->hasSpace(out, 3)) break; // try again next chunk
    send(paced.output, paced.bytes, 3);
    _pacedNext++;
  }
}

void Scenes::publish()
{
  SceneStatus status;
  status.pending = _pending;
  status.scene = _active.scene;
  status.timing = _active.content.timing;
  status.ticksLeft = _pending && _boundaryTick >= 0 ? (int32_t)(_boundaryTick - (int64_t)_tick) : -1;
  status.fired = _fired;
  status.pacedLeft = (uint8_t)(_pacedCount - _pacedNext);
  CriticalGuard guard(_lock);
  _status = status;
}

void Scenes::run(uint32_t nowMillis)
{
  Plan plan;
  bool hasPlan, cancelled;
  {
    CriticalGuard guard(_lock);
    hasPlan = _planSet;
    cancelled = _cancel;
    if (hasPlan) plan = _plan;
    _planSet = false;
    _cancel = false;
  }
  if (cancelled) { // panic: nothing more from scenes
    _pending = false;
    _pacedCount = _pacedNext = 0;
  }
  if (hasPlan) startPlan(plan, nowMillis);
  if (_pending && (_fireNow || !running())) fire();
  pace(nowMillis);
  publish();
}

void Scenes::onClock(const ClockEvent &event, void *ctx)
{
  Scenes *scenes = static_cast<Scenes *>(ctx);
  switch (event.type) {
    case ClockEvent::Tick:
      scenes->_tick = event.tick;
      if (scenes->_pending) scenes->sendLeadPrograms(false);
      break;
    case ClockEvent::Bar:
      scenes->_tick = event.tick;
      if (scenes->_pending && !scenes->_waitPhrase && scenes->_boundaryTick >= 0 &&
          (int64_t)event.tick >= scenes->_boundaryTick) {
        scenes->fire();
      }
      break;
    case ClockEvent::Start:
      scenes->_running = true;
      scenes->_tick = 0;
      break;
    case ClockEvent::Continue:
      scenes->_running = true;
      break;
    case ClockEvent::Stop:
      scenes->_running = false; // a pending recall fires in run()
      break;
    default:
      break;
  }
}

void Scenes::onPhrase(uint16_t, uint64_t, void *ctx)
{
  Scenes *scenes = static_cast<Scenes *>(ctx);
  if (scenes->_pending && scenes->_waitPhrase) scenes->fire();
}

void Scenes::observeInput(const MidiMessage &msg, bool, void *ctx)
{
  Scenes *scenes = static_cast<Scenes *>(ctx);
  if (!msg.isChannel() || (msg.status() & 0xF0) != 0xB0) return;
  CriticalGuard guard(scenes->_lock);
  if (!scenes->_learnOn) return;
  int next = (scenes->_learnedHead + 1) % kLearned;
  if (next == scenes->_learnedTail) return; // full: the UI hasn't caught up
  scenes->_learned[scenes->_learnedHead] = {msg.channel(), msg.bytes[1], msg.bytes[2]};
  scenes->_learnedHead = next;
}
