#include "clock_engine.h"

#include <Arduino.h>
#include <Preferences.h>
#include <math.h>

#include "midi_outputs.h"
#include "platform_time.h"

static constexpr uint32_t kSaveDelayMillis = 2000;
static constexpr uint64_t kLostMinUs = 500000; // external clock lost after 3 periods or 500 ms, whichever is longer
static const char *const kNamespace = "oximate";
static const char *const kKey = "clock";
static constexpr uint8_t kClockOwner = 8; // Origin::internal owner id: stage 8

// ---------------------------------------------------------------------------- UI side

void ClockEngine::begin(MidiOutputs &outputs)
{
  _outputs = &outputs;
  load();
  _pending = _active = _edit;
  _nextTickUs = nowMicros64() + periodUs();
}

void ClockEngine::changed()
{
  {
    CriticalGuard guard(_lock);
    _pending = _edit;
    _pendingSet = true;
  }
  _dirty = true;
  _changedMillis = millis();
}

void ClockEngine::poll()
{
  if (_dirty && millis() - _changedMillis >= kSaveDelayMillis) {
    save();
    _dirty = false;
  }
}

void ClockEngine::pushCommand(Command command, uint16_t arg)
{
  CriticalGuard guard(_lock);
  if (_commandCount < kCommands) {
    _commands[_commandCount] = command;
    _commandArgs[_commandCount] = arg;
    _commandCount++;
  }
}

void ClockEngine::start() { pushCommand(Command::Start); }
void ClockEngine::stop() { pushCommand(Command::Stop); }
void ClockEngine::cont() { pushCommand(Command::Continue); }
void ClockEngine::locate(uint16_t sixteenths) { pushCommand(Command::Locate, sixteenths); }

ClockEngine::Snapshot ClockEngine::snapshot() const
{
  CriticalGuard guard(_lock);
  return _snapshot;
}

void ClockEngine::migrateFromRouter(ClockMode mode, bool clockOff)
{
  if (_savedConfig) {
    return;
  }
  _edit.mode = mode;
  if (clockOff) {
    for (ClockOutputConfig &output : _edit.outputs) {
      output.enabled = false;
      output.transport = false;
    }
  }
  changed();
}

void ClockEngine::load()
{
  Preferences prefs;
  if (!prefs.begin(kNamespace, true)) {
    return;
  }
  ClockConfig stored;
  if (prefs.getBytesLength(kKey) == sizeof(stored) && prefs.getBytes(kKey, &stored, sizeof(stored)) == sizeof(stored) &&
      stored.version == ClockConfig::kVersion) {
    _edit = stored;
    _savedConfig = true;
  }
  prefs.end();
}

void ClockEngine::save()
{
  Preferences prefs;
  if (prefs.begin(kNamespace, false)) {
    prefs.putBytes(kKey, &_edit, sizeof(_edit));
    prefs.end();
  }
}

// ---------------------------------------------------------------------------- MIDI task side

void ClockEngine::addListener(Listener listener, void *ctx)
{
  if (_listenerCount < kMaxListeners) {
    _listeners[_listenerCount] = listener;
    _listenerCtx[_listenerCount] = ctx;
    _listenerCount++;
  }
}

bool ClockEngine::following(MidiSource source) const
{
  return (_active.mode == ClockMode::FollowUsb && source == MidiSource::Usb) ||
         (_active.mode == ClockMode::FollowDin && source == MidiSource::Din);
}

double ClockEngine::periodUs() const
{
  if (_active.mode != ClockMode::Internal && _extPeriodUs > 0) {
    return _extPeriodUs;
  }
  float bpm = _active.bpm;
  if (bpm < kMinBpm) bpm = kMinBpm;
  if (bpm > kMaxBpm) bpm = kMaxBpm;
  return 60e6 / (24.0 * bpm);
}

bool ClockEngine::schedule(uint64_t atUs, Callback callback, void *ctx, uint32_t arg)
{
  if (_scheduledCount >= kMaxScheduled) {
    return false;
  }
  int i = _scheduledCount++;
  _scheduled[i] = {atUs, callback, ctx, arg};
  while (i > 0) { // sift up
    int parent = (i - 1) / 2;
    if (_scheduled[parent].timeUs <= _scheduled[i].timeUs) break;
    Scheduled tmp = _scheduled[parent];
    _scheduled[parent] = _scheduled[i];
    _scheduled[i] = tmp;
    i = parent;
  }
  return true;
}

void ClockEngine::popScheduled(Scheduled &out)
{
  out = _scheduled[0];
  _scheduled[0] = _scheduled[--_scheduledCount];
  int i = 0;
  for (;;) { // sift down
    int left = 2 * i + 1, right = left + 1, smallest = i;
    if (left < _scheduledCount && _scheduled[left].timeUs < _scheduled[smallest].timeUs) smallest = left;
    if (right < _scheduledCount && _scheduled[right].timeUs < _scheduled[smallest].timeUs) smallest = right;
    if (smallest == i) break;
    Scheduled tmp = _scheduled[smallest];
    _scheduled[smallest] = _scheduled[i];
    _scheduled[i] = tmp;
    i = smallest;
  }
}

uint64_t ClockEngine::nextDueUs() const
{
  uint64_t next = UINT64_MAX;
  if (_active.mode == ClockMode::Internal) {
    next = (uint64_t)_nextTickUs;
  }
  if (_scheduledCount && _scheduled[0].timeUs < next) {
    next = _scheduled[0].timeUs;
  }
  return next;
}

void ClockEngine::emit(ClockEvent::Type type, uint64_t timeUs, uint32_t step)
{
  ClockEvent event = {type, _tick, step, timeUs};
  for (int i = 0; i < _listenerCount; i++) {
    _listeners[i](event, _listenerCtx[i]);
  }
}

void ClockEngine::sendTick(int output)
{
  static const uint8_t clock = 0xF8;
  MidiMessage msg = {MidiSource::Din, 0, &clock, 1};
  _outputs->send((MidiOutput)output, msg, Origin::internal(kClockOwner));
}

void ClockEngine::sendTransport(uint8_t status)
{
  uint8_t bytes[3] = {status, 0, 0};
  uint16_t length = 1;
  if (status == 0xF2) { // song position in 16ths
    uint32_t sixteenths = _tick / 6;
    bytes[1] = sixteenths & 0x7F;
    bytes[2] = (sixteenths >> 7) & 0x7F;
    length = 3;
  }
  MidiMessage msg = {MidiSource::Din, 0, bytes, length};
  for (int o = 0; o < (int)ClockOutput::Count; o++) {
    if (_active.outputs[o].transport) {
      _outputs->send((MidiOutput)o, msg, Origin::internal(kClockOwner));
    }
  }
}

void ClockEngine::extraTickCallback(void *ctx, uint32_t output, uint64_t)
{
  static_cast<ClockEngine *>(ctx)->sendTick(output);
}

void ClockEngine::stepCallback(void *ctx, uint32_t step, uint64_t timeUs)
{
  ClockEngine *engine = static_cast<ClockEngine *>(ctx);
  if (engine->_running) {
    engine->emit(ClockEvent::Step, timeUs, step);
  }
}

/*! Swing moves the second step of each pair later: a step at offset o (in ticks) within a
 *  pair of length L lands at o * 2s before the middle, and L*s + (o - L/2) * 2(1-s) after it */
uint32_t ClockEngine::swingDelayTicksX1000(uint32_t step) const
{
  double s = _active.swing / 100.0;
  if (s <= 0.5) return 0;
  if (s > 0.75) s = 0.75;
  double pair = _active.swingGrid == 8 ? 24 : 12;
  double offset = fmod(step * 6.0, pair);
  double warped = offset < pair / 2 ? offset * 2 * s : pair * s + (offset - pair / 2) * 2 * (1 - s);
  return (uint32_t)((warped - offset) * 1000 + 0.5);
}

void ClockEngine::tick(uint64_t timeUs)
{
  emit(ClockEvent::RawTick, timeUs); // also while stopped; Tick below only while running
  double period = periodUs();
  for (int o = 0; o < (int)ClockOutput::Count; o++) {
    const ClockOutputConfig &output = _active.outputs[o];
    if (!output.enabled) continue;
    int ratio = output.ratio;
    if (ratio <= -2) {
      if (_outTicks[o] % (uint32_t)(-ratio) == 0) sendTick(o);
    } else {
      sendTick(o);
      for (int k = 1; k < ratio; k++) {
        schedule(timeUs + (uint64_t)(k * period / ratio), extraTickCallback, this, o);
      }
    }
    _outTicks[o]++;
  }

  if (!_running) {
    return;
  }
  emit(ClockEvent::Tick, timeUs);
  if (_tick % 6 == 0) {
    uint32_t step = _tick / 6;
    uint32_t delay = swingDelayTicksX1000(step);
    if (delay == 0) {
      emit(ClockEvent::Step, timeUs, step);
    } else {
      schedule(timeUs + (uint64_t)(delay * period / 1000), stepCallback, this, step);
    }
  }
  if (_tick % 24 == 0) {
    emit(ClockEvent::Beat, timeUs);
    if ((_tick / 24) % _active.beatsPerBar == 0) {
      emit(ClockEvent::Bar, timeUs);
    }
  }
  _tick++;
}

void ClockEngine::doStart(uint64_t nowUs, bool sendMidi)
{
  _running = true;
  _tick = 0;
  for (uint32_t &t : _outTicks) t = 0; // dividers line up with the downbeat
  if (sendMidi) sendTransport(0xFA);
  if (_active.mode == ClockMode::Internal) {
    _nextTickUs = nowUs + 1000; // the first tick after Start is beat 1
  }
  emit(ClockEvent::Start, nowUs);
}

void ClockEngine::doStop(uint64_t nowUs, bool sendMidi)
{
  _running = false;
  if (sendMidi) sendTransport(0xFC);
  emit(ClockEvent::Stop, nowUs);
}

void ClockEngine::doContinue(uint64_t nowUs, bool sendMidi)
{
  _running = true;
  if (sendMidi) sendTransport(0xFB);
  emit(ClockEvent::Continue, nowUs);
}

bool ClockEngine::onExternal(const MidiMessage &msg)
{
  if (!following(msg.source)) {
    return false;
  }
  uint64_t nowUs = nowMicros64();
  switch (msg.status()) {
    case 0xF8: {
      if (_haveExt) {
        double interval = (double)(uint32_t)(msg.timeMicros - _lastExtMicros);
        if (_extPeriodUs <= 0) {
          _extPeriodUs = interval;
        } else if (fabs(interval - _extPeriodUs) <= 0.25 * _extPeriodUs) {
          _extPeriodUs += (interval - _extPeriodUs) / 24; // ~24-tick moving average
          if (_consistent < kLockTicks) _consistent++;
        } else if (!_locked) {
          _extPeriodUs = interval; // still settling: start over from this interval
          _consistent = 0;
        } // else: outlier while locked, ignored
        _locked = _consistent >= kLockTicks;
      }
      _haveExt = true;
      _lost = false;
      _lastExtMicros = msg.timeMicros;
      _lastExtUs = nowUs;
      tick(nowUs); // passed straight on for the lowest latency
      break;
    }
    case 0xFA:
      doStart(nowUs, true);
      break;
    case 0xFB:
      doContinue(nowUs, true);
      break;
    case 0xFC:
      doStop(nowUs, true);
      break;
    case 0xF2:
      if (!_running && msg.length == 3) {
        _tick = ((msg.bytes[2] << 7) | msg.bytes[1]) * 6;
        sendTransport(0xF2);
        emit(ClockEvent::Locate, nowUs);
      }
      break;
  }
  return true;
}

void ClockEngine::applyPending(uint64_t nowUs)
{
  ClockConfig config;
  {
    CriticalGuard guard(_lock);
    if (!_pendingSet) return;
    config = _pending;
    _pendingSet = false;
  }
  bool modeChanged = config.mode != _active.mode;
  _active = config;
  if (modeChanged) {
    _haveExt = false;
    _extPeriodUs = 0;
    _consistent = 0;
    _locked = false;
    _lost = false;
    if (_active.mode == ClockMode::Internal) {
      _nextTickUs = nowUs + periodUs();
    }
  }
}

void ClockEngine::runCommands(uint64_t nowUs)
{
  Command commands[kCommands];
  uint16_t args[kCommands];
  int count;
  {
    CriticalGuard guard(_lock);
    count = _commandCount;
    for (int i = 0; i < count; i++) {
      commands[i] = _commands[i];
      args[i] = _commandArgs[i];
    }
    _commandCount = 0;
  }
  // Transport buttons only act on the internal clock; a followed clock is in charge otherwise
  bool internal = _active.mode == ClockMode::Internal;
  for (int i = 0; i < count; i++) {
    switch (commands[i]) {
      case Command::Start:
        if (internal) doStart(nowUs, true);
        break;
      case Command::Stop:
        if (internal && _running) doStop(nowUs, true);
        break;
      case Command::Continue:
        if (internal && !_running) doContinue(nowUs, true);
        break;
      case Command::Locate:
        if (internal && !_running) {
          _tick = args[i] * 6u;
          sendTransport(0xF2);
          emit(ClockEvent::Locate, nowUs);
        }
        break;
    }
  }
}

void ClockEngine::runDue(uint64_t nowUs)
{
  applyPending(nowUs);
  runCommands(nowUs);

  if (_active.mode == ClockMode::Internal) {
    double period = periodUs();
    if (nowUs > _nextTickUs + 4 * period) {
      _nextTickUs = nowUs; // fell far behind (e.g. at boot): resync instead of bursting
    }
    while (nowUs >= _nextTickUs) {
      tick((uint64_t)_nextTickUs);
      _nextTickUs += periodUs(); // a tempo change takes effect from the next tick
    }
  } else if (_haveExt) {
    uint64_t timeout = (uint64_t)(3 * _extPeriodUs);
    if (timeout < kLostMinUs) timeout = kLostMinUs;
    if (nowUs - _lastExtUs > timeout) {
      _haveExt = false;
      _locked = false;
      _consistent = 0;
      _lost = true;
      if (_running) doStop(nowUs, true); // decided 2026-11-01: a lost clock stops OxiMate
    }
  }

  while (_scheduledCount && _scheduled[0].timeUs <= nowUs) {
    Scheduled due;
    popScheduled(due);
    due.callback(due.ctx, due.arg, due.timeUs);
  }

  Snapshot snap;
  snap.mode = _active.mode;
  snap.bpm = _active.mode == ClockMode::Internal ? _active.bpm : (_extPeriodUs > 0 ? 60e6 / (24 * _extPeriodUs) : 0);
  snap.running = _running;
  snap.locked = _locked;
  snap.lost = _lost;
  uint32_t current = _tick ? _tick - 1 : 0;
  uint8_t beatsPerBar = _active.beatsPerBar ? _active.beatsPerBar : 4;
  snap.tick = current;
  snap.beat = (current / 24) % beatsPerBar;
  snap.bar = current / (24 * beatsPerBar);
  snap.beatsPerBar = beatsPerBar;
  CriticalGuard guard(_lock);
  _snapshot = snap;
}
