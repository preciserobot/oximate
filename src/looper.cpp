#include "looper.h"

#include <Arduino.h>
#include <Preferences.h>
#include <stdlib.h>
#include <string.h>

#include "input_observers.h"
#include "note_tracker.h"
#include "phrase_tracker.h"

#if defined(ESP_PLATFORM)
#include <esp_heap_caps.h>
static LoopEvent *allocateEvents(uint32_t count)
{
  return (LoopEvent *)heap_caps_malloc(count * sizeof(LoopEvent), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}
#else
static LoopEvent *allocateEvents(uint32_t count) { return (LoopEvent *)malloc(count * sizeof(LoopEvent)); }
#endif

static const char *const kNamespace = "oximate";
static const char *const kKey = "loop";
static constexpr uint32_t kPublishMillis = 20;
static constexpr int kMaxHeld = 32;

uint32_t Looper::quantiseTicks(uint8_t setting)
{
  switch (setting) {
    case 1: return LoopBuffer::kPpqn / 4; // 1/16
    case 2: return LoopBuffer::kPpqn / 2; // 1/8
    case 3: return LoopBuffer::kPpqn / 6; // 1/16 triplets
    default: return 0;
  }
}

// ---------------------------------------------------------------------------- UI side

bool Looper::begin(MidiOutputs &outputs, ClockEngine &clock, PhraseTracker *phrase, uint32_t capacity)
{
  _outputs = &outputs;
  _clock = &clock;
  _capacity = capacity;
  LoopEvent *events = allocateEvents(capacity);
  LoopEvent *pending = allocateEvents(kPendingCapacity);
  _transfer = allocateEvents(capacity);
  if (!events || !pending || !_transfer) {
    _capacity = 0;
    return false;
  }
  _buffer.attach(events, capacity, pending, kPendingCapacity);
  load();
  _active = _pending = _edit;
  clock.addListener(onClock, this);
  InputHooks::addObserver(observer, this);
  if (phrase) phrase->addBoundaryListener(onPhrase, this);
  publish();
  return true;
}

void Looper::changed()
{
  {
    CriticalGuard guard(_lock);
    _pending = _edit;
    _pendingSet = true;
  }
  _dirty = true;
  _changedMillis = millis();
}

void Looper::poll()
{
  if (_dirty && millis() - _changedMillis >= kSaveDelayMillis) {
    save();
    _dirty = false;
  }
}

void Looper::command(uint8_t cmd)
{
  CriticalGuard guard(_lock);
  _commands |= cmd;
}

void Looper::rec() { command(CmdRec); }
void Looper::undo() { command(CmdUndo); }
void Looper::clear() { command(CmdClear); }
void Looper::togglePlay() { command(CmdPlay); }
void Looper::toggleOverdub() { command(CmdOverdub); }

LoopSnapshot Looper::snapshot() const
{
  CriticalGuard guard(_lock);
  return _snapshot;
}

void Looper::requestExport()
{
  _exportReady = false;
  _exportRequested = true;
}

void Looper::importTransfer(uint32_t count, uint32_t length96)
{
  CriticalGuard guard(_lock);
  _importCount = count;
  _importLength = length96;
  _importPending = true;
}

void Looper::load()
{
  Preferences prefs;
  if (!prefs.begin(kNamespace, true)) return;
  LoopConfig stored;
  if (prefs.getBytesLength(kKey) == sizeof(stored) && prefs.getBytes(kKey, &stored, sizeof(stored)) == sizeof(stored) &&
      stored.version == LoopConfig::kVersion) {
    _edit = stored;
  }
  prefs.end();
}

void Looper::save()
{
  Preferences prefs;
  if (prefs.begin(kNamespace, false)) {
    prefs.putBytes(kKey, &_edit, sizeof(_edit));
    prefs.end();
  }
}

// ---------------------------------------------------------------------------- MIDI task side

void Looper::send(uint8_t status, uint8_t data1, uint8_t data2)
{
  uint8_t bytes[3] = {status, data1, data2};
  MidiMessage msg = {MidiSource::Din, 0, bytes, (uint16_t)(1 + midiDataLength(status))};
  if (_active.outputs & 1) _outputs->send(MidiOutput::Din, msg, Origin::internal(kOwner));
  if (_active.outputs & 2) _outputs->send(MidiOutput::Usb, msg, Origin::internal(kOwner));
}

void Looper::releaseAll()
{
  Origin origin = Origin::internal(kOwner);
  _outputs->tracker().release(*_outputs, MidiOutput::Din, &origin);
  _outputs->tracker().release(*_outputs, MidiOutput::Usb, &origin);
}

void Looper::scheduledCallback(void *ctx, uint32_t packed, uint64_t)
{
  Looper *looper = static_cast<Looper *>(ctx);
  if (!looper->_loopActive || !looper->_playing) return; // stopped in the meantime: its notes were released
  looper->send(packed >> 16, (packed >> 8) & 0x7F, packed & 0x7F);
}

/*! Events of one 24 PPQN tick (4 looper ticks): those on the tick go now, the others at their
 *  exact time through the clock engine's scheduler */
void Looper::playWindow(uint32_t position, uint64_t tickUs)
{
  uint32_t from = position * 4;
  for (uint32_t i = _buffer.lowerBound(from); i < _buffer.count() && _buffer[i].tick < from + 4; i++) {
    const LoopEvent &e = _buffer[i];
    uint32_t sub = e.tick - from;
    uint32_t packed = (uint32_t)e.status << 16 | (uint32_t)e.data1 << 8 | e.data2;
    if (sub == 0 || !_clock->schedule(tickUs + (uint64_t)(sub * _periodUs / 4), scheduledCallback, this, packed)) {
      send(e.status, e.data1, e.data2); // scheduler full: a sub-tick early is better than lost
    }
  }
}

void Looper::startRecording()
{
  ClockEngine::Snapshot clock = _clock->snapshot();
  uint8_t beatsPerBar = clock.beatsPerBar ? clock.beatsPerBar : 4;
  uint8_t bars = _active.bars ? _active.bars : 4;
  _buffer.clear();
  _length = (uint32_t)bars * beatsPerBar * 24;
  _buffer.setLength(_length * 4);
  _state = LoopState::Recording;
  _pass = 0;
  _position = 0;
  _loopActive = true;
  _overdub = false;
  _changes++;
}

/*! The loop end: the pass being recorded joins the loop; notes still held are cut here and,
 *  while overdubbing continues, restarted at the start */
void Looper::wrap()
{
  LoopBuffer::HeldNote held[kMaxHeld];
  uint32_t quantise = quantiseTicks(_active.quantise);
  if (_state == LoopState::Recording) {
    _buffer.commit(quantise, nullptr, 0);
    _state = LoopState::Looping;
    _overviewDirty = true;
    _changes++;
  } else if (_state == LoopState::Looping && _overdub) {
    int n = _buffer.commit(quantise, held, kMaxHeld);
    for (int i = 0; i < n; i++) _buffer.record(0, held[i].status, held[i].note, held[i].velocity, _pass, false);
    _overviewDirty = true;
    _changes++;
  }
}

void Looper::stopRunning()
{
  if (_state == LoopState::Recording) {
    _buffer.discardPending(); // an unfinished first pass is dropped; it records again at Start
    _state = LoopState::Armed;
  } else if (_overdub) {
    _buffer.commit(quantiseTicks(_active.quantise), nullptr, 0);
    _overviewDirty = true;
  }
  _overdub = false;
  _loopActive = false;
  _waitBar = _state == LoopState::Looping;
  releaseAll();
  _changes++;
}

void Looper::onClock(const ClockEvent &event, void *ctx) { static_cast<Looper *>(ctx)->handle(event); }

void Looper::handle(const ClockEvent &event)
{
  switch (event.type) {
    case ClockEvent::Tick: {
      ClockEngine::Snapshot clock = _clock->snapshot();
      if (clock.bpm > 0) _periodUs = 60e6 / (24.0 * clock.bpm);
      _lastTickUs = event.timeUs;
      if (!_loopActive || !_length) break;
      if (++_position >= _length) {
        _position = 0;
        wrap();
      }
      if (_state == LoopState::Looping && _playing) playWindow(_position, event.timeUs);
      break;
    }
    case ClockEvent::Bar:
      _lastTickUs = event.timeUs;
      if (_state == LoopState::Armed && _running && (!_active.startOnPhrase || _phraseNow)) {
        startRecording();
      } else if (_waitBar && _state == LoopState::Looping) {
        _waitBar = false;
        _loopActive = true;
        _position = 0;
        if (_playing) playWindow(0, event.timeUs);
      }
      _phraseNow = false;
      break;
    case ClockEvent::Start:
    case ClockEvent::Continue:
      _running = true;
      if (_state == LoopState::Looping) {
        _loopActive = false;
        _waitBar = true; // loops always start on a bar line
      }
      break;
    case ClockEvent::Stop:
      _running = false;
      stopRunning();
      break;
    default:
      break;
  }
}

void Looper::observer(const MidiMessage &msg, bool forwarded, void *ctx)
{
  Looper *looper = static_cast<Looper *>(ctx);
  // Only what was actually routed: echoes and filtered messages aren't recorded
  if (!forwarded || !msg.isChannel() || (msg.status() & 0xF0) == 0xC0) return;
  bool recording = looper->_state == LoopState::Recording ||
                   (looper->_state == LoopState::Looping && looper->_overdub && looper->_loopActive);
  if (!recording) return;
  uint8_t inputBit = 1 << (int)msg.source; // USB 1, DIN 2, BLE 4, network 8
  if (!(looper->_active.inputs & inputBit)) return;
  if (looper->_active.channel && msg.channel() != looper->_active.channel) return;

  // Position between two clock ticks from the message's own timestamp
  uint32_t sub = 0;
  if (msg.timeMicros && looper->_lastTickUs) {
    int32_t since = (int32_t)(msg.timeMicros - (uint32_t)looper->_lastTickUs);
    if (since > 0) {
      sub = (uint32_t)(since * 4 / looper->_periodUs);
      if (sub > 3) sub = 3;
    }
  }
  uint8_t data2 = msg.length > 2 ? msg.bytes[2] : 0;
  looper->_buffer.record(looper->_position * 4 + sub, msg.status(), msg.bytes[1], data2, looper->_pass,
                         msg.source == MidiSource::Din);
}

void Looper::onPhrase(uint16_t, uint64_t, void *ctx)
{
  Looper *looper = static_cast<Looper *>(ctx);
  // Called on the bar's first tick, before its Bar event: recording starts on that Bar
  if (looper->_state == LoopState::Armed && looper->_active.startOnPhrase) looper->_phraseNow = true;
}

void Looper::run(uint32_t nowMillis)
{
  uint8_t commands;
  bool configChanged, import;
  LoopConfig next;
  uint32_t importCount, importLength;
  {
    CriticalGuard guard(_lock);
    commands = _commands;
    _commands = 0;
    configChanged = _pendingSet;
    next = _pending;
    _pendingSet = false;
    import = _importPending;
    _importPending = false;
    importCount = _importCount;
    importLength = _importLength;
  }

  if (configChanged) {
    if (next.outputs != _active.outputs) releaseAll(); // notes started on the old outputs end there
    _active = next;
  }

  if (commands & CmdClear) {
    releaseAll();
    _buffer.clear();
    _state = LoopState::Empty;
    _overdub = false;
    _loopActive = false;
    _waitBar = false;
    _length = 0;
    _overviewDirty = true;
    _changes++;
  }
  if (commands & CmdRec) {
    switch (_state) {
      case LoopState::Empty: _state = LoopState::Armed; break;
      case LoopState::Armed: _state = LoopState::Empty; break;
      case LoopState::Recording: // cancel the first pass
        _buffer.discardPending();
        _buffer.clear();
        _state = LoopState::Empty;
        _loopActive = false;
        _length = 0;
        releaseAll();
        break;
      default: break; // a loop exists: overdub (C) or clear first
    }
    _changes++;
  }
  if (commands & CmdUndo) {
    releaseAll();
    if (_overdub) {
      _buffer.discardPending(); // undoing during an overdub drops the pass being recorded
      _overdub = false;
    } else if (_state == LoopState::Looping) {
      _buffer.undo();
      if (!_buffer.count()) {
        _state = LoopState::Empty;
        _loopActive = false;
        _waitBar = false;
        _length = 0;
      }
    }
    _overviewDirty = true;
    _changes++;
  }
  if (commands & CmdPlay) {
    _playing = !_playing;
    if (!_playing) releaseAll();
    _changes++;
  }
  if ((commands & CmdOverdub) && _state == LoopState::Looping) {
    if (!_overdub) {
      uint8_t passes = _buffer.passes();
      if (passes < LoopBuffer::kMaxPasses) {
        _pass = passes;
        _overdub = true;
      }
    } else {
      _buffer.commit(quantiseTicks(_active.quantise), nullptr, 0); // notes still held are cut at the end
      _overdub = false;
      _overviewDirty = true;
    }
    _changes++;
  }

  if (import && _buffer.load(_transfer, importCount, importLength)) {
    releaseAll();
    _length = importLength / 4;
    _state = LoopState::Looping;
    _overdub = false;
    _loopActive = false;
    _waitBar = _running; // joins at the next bar line
    _overviewDirty = true;
    _changes++;
  }

  if (_exportRequested.exchange(false)) {
    memcpy(_transfer, _buffer.events(), _buffer.count() * sizeof(LoopEvent));
    _transferCount = _buffer.count();
    _transferLength = _buffer.length();
    _exportReady = true;
  }

  if (_overviewDirty || _changes != _snapshot.changes || nowMillis - _lastPublish >= kPublishMillis) {
    _lastPublish = nowMillis;
    publish();
  }
}

/*! Snapshot for the UI; the overview (busiest 4 channels, note-ons in 64 columns) is rebuilt
 *  only when the content changed */
void Looper::publish()
{
  if (_overviewDirty) {
    _overviewDirty = false;
    uint32_t perChannel[16] = {};
    for (uint32_t i = 0; i < _buffer.count(); i++) {
      if ((_buffer[i].status & 0xF0) == 0x90 && _buffer[i].data2) perChannel[_buffer[i].status & 0x0F]++;
    }
    for (int row = 0; row < 4; row++) {
      int best = -1;
      for (int ch = 0; ch < 16; ch++) {
        bool taken = false;
        for (int r = 0; r < row; r++) taken |= _rowChannel[r] == ch + 1;
        if (!taken && perChannel[ch] && (best < 0 || perChannel[ch] > perChannel[best])) best = ch;
      }
      _rowChannel[row] = best >= 0 ? best + 1 : 0;
      _rowBits[row] = 0;
    }
    uint32_t length = _buffer.length();
    for (uint32_t i = 0; i < _buffer.count() && length; i++) {
      const LoopEvent &e = _buffer[i];
      if ((e.status & 0xF0) != 0x90 || !e.data2) continue;
      for (int row = 0; row < 4; row++) {
        if (_rowChannel[row] == (e.status & 0x0F) + 1) {
          _rowBits[row] |= 1ull << (uint64_t)e.tick * 64 / length;
        }
      }
    }
  }
  LoopSnapshot s;
  s.state = _state;
  s.active = _loopActive;
  s.playing = _playing;
  s.overdub = _overdub;
  s.bars = _active.bars;
  s.passes = _buffer.passes();
  s.events = _buffer.count();
  s.position = _position;
  s.length = _length;
  memcpy(s.rowChannel, _rowChannel, sizeof(_rowChannel));
  memcpy(s.rowBits, _rowBits, sizeof(_rowBits));
  s.changes = _changes;
  CriticalGuard guard(_lock);
  _snapshot = s;
}
