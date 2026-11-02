#include "phrase_tracker.h"

#include <Arduino.h>
#include <Preferences.h>
#include <math.h>

#include "cue_queue.h"

static constexpr uint32_t kSaveDelayMillis = 2000;
static const char *const kNamespace = "oximate";
static const char *const kKey = "phrase";

// ---------------------------------------------------------------------------- UI side

void PhraseTracker::begin(ClockEngine &clock)
{
  _clock = &clock;
  load();
  _pending = _active = _edit;
  clock.addListener(onClock, this);
  publish();
}

void PhraseTracker::changed()
{
  {
    CriticalGuard guard(_lock);
    _pending = _edit;
    _pendingSet = true;
  }
  _dirty = true;
  _changedMillis = millis();
}

void PhraseTracker::poll()
{
  // Like the drum machine: no flash write while playing
  if (_dirty && millis() - _changedMillis >= kSaveDelayMillis && !snapshot().running) {
    save();
    _dirty = false;
  }
}

void PhraseTracker::realign(uint64_t tapUs, bool phraseOnly)
{
  CriticalGuard guard(_lock);
  _command = phraseOnly ? Command::RealignPhrase : Command::Realign;
  _commandUs = tapUs;
}

void PhraseTracker::reset()
{
  CriticalGuard guard(_lock);
  _command = Command::Reset;
}

PhraseSnapshot PhraseTracker::snapshot() const
{
  CriticalGuard guard(_lock);
  return _snapshot;
}

void PhraseTracker::load()
{
  Preferences prefs;
  if (!prefs.begin(kNamespace, true)) return;
  PhraseConfig stored;
  if (prefs.getBytesLength(kKey) == sizeof(stored) && prefs.getBytes(kKey, &stored, sizeof(stored)) == sizeof(stored) &&
      stored.version == PhraseConfig::kVersion) {
    _edit = stored;
  }
  prefs.end();
}

void PhraseTracker::save()
{
  Preferences prefs;
  if (prefs.begin(kNamespace, false)) {
    prefs.putBytes(kKey, &_edit, sizeof(_edit));
    prefs.end();
  }
}

// ---------------------------------------------------------------------------- MIDI task side

void PhraseTracker::addBoundaryListener(BoundaryListener listener, void *ctx)
{
  if (_listenerCount < kMaxListeners) {
    _listeners[_listenerCount] = listener;
    _listenerCtx[_listenerCount++] = ctx;
  }
}

void PhraseTracker::onClock(const ClockEvent &event, void *ctx)
{
  PhraseTracker *tracker = static_cast<PhraseTracker *>(ctx);
  ClockEngine::Snapshot clock = tracker->_clock->snapshot();
  double period = clock.bpm > 0 ? 60e6 / (24.0 * clock.bpm) : 0;
  tracker->_external = clock.mode != ClockMode::Internal;
  tracker->handle(event, clock.beatsPerBar, period);
}

void PhraseTracker::handle(const ClockEvent &event, uint8_t beatsPerBar, double periodUs)
{
  if (beatsPerBar) _beatsPerBar = beatsPerBar;
  if (periodUs > 0) _periodUs = periodUs;
  switch (event.type) {
    case ClockEvent::Start:
      _running = true;
      _stopped = false;
      _ticks = 0;
      _offsetBars = 0;
      _synced = true;
      _phraseStarts++;
      Cues::post(Cue::PhraseStart); // bar 1 starts with the first tick, ~1 ms from now
      publish();
      break;
    case ClockEvent::Continue:
      _running = true;
      _stopped = false;
      publish();
      break;
    case ClockEvent::Stop:
      _running = false;
      _stopped = true;
      publish();
      break;
    case ClockEvent::Locate: // Song Position: the engine's tick is the located position
      _ticks = event.tick;
      if (_active.sppRealigns) {
        _offsetBars = 0;
        _synced = true;
      }
      publish();
      break;
    case ClockEvent::RawTick:
      _seenTick = true;
      // While stopped, internal clock keeps ticking but there is no musical position. A master
      // that was already playing when OxiMate was plugged in sends ticks without a Start:
      // count those too, marked as not synced until a Bar 1 tap.
      if (_running || (_external && !_stopped)) countTick(event.timeUs);
      break;
    default:
      break;
  }
}

void PhraseTracker::countTick(uint64_t timeUs)
{
  uint32_t t = _ticks;
  uint32_t ticksPerBar = 24u * _beatsPerBar;
  _lastTickUs = timeUs;
  _ticks++;
  if (t % 24 != 0) return; // nothing changes between beats

  int32_t phraseBars = _active.phraseBars ? _active.phraseBars : 16;
  if (t % ticksPerBar == 0) {
    int32_t bar = (int32_t)(t / ticksPerBar) - _offsetBars;
    int32_t barInPhrase = floorMod(bar, phraseBars);
    if (barInPhrase == 0) {
      _phraseStarts++;
      uint16_t phrase = (uint16_t)floorDiv(bar, phraseBars);
      for (int i = 0; i < _listenerCount; i++) _listeners[i](phrase, timeUs, _listenerCtx[i]);
    }
    // Cues for the next bar are posted one bar ahead, so the pulse can land on its downbeat
    // (the haptics player of stage 10 subtracts the motor's spin-up time)
    if (_synced) {
      uint32_t nextBarUs = (uint32_t)(timeUs + (uint64_t)(ticksPerBar * _periodUs));
      int32_t next = floorMod(bar + 1, phraseBars);
      int32_t warn = _active.warnBars < phraseBars ? _active.warnBars : 0;
      if (next == 0) {
        Cues::post(Cue::PhraseStart, nextBarUs);
      } else if (warn && next == phraseBars - warn) {
        Cues::post(Cue::PhraseEnding, nextBarUs);
      }
    }
  }
  publish();
}

void PhraseTracker::doRealign(uint64_t tapUs, bool phraseOnly)
{
  uint32_t ticksPerBar = 24u * _beatsPerBar;
  double position = _ticks ? _ticks - 1 : 0;
  if (_lastTickUs && _periodUs > 0) {
    position += ((double)(int64_t)(tapUs - _lastTickUs)) / _periodUs;
  }
  // Nearest bar line: a tap slightly before or after the downbeat still lands on it
  int32_t barLine = (int32_t)llround(position / ticksPerBar);
  if (phraseOnly) {
    int32_t phraseBars = _active.phraseBars ? _active.phraseBars : 16;
    int32_t phrase = (int32_t)lround((double)(barLine - _offsetBars) / phraseBars);
    _offsetBars = barLine - phrase * phraseBars; // restart the phrase, keep the section count
  } else {
    _offsetBars = barLine;
  }
  _synced = true;
}

void PhraseTracker::run()
{
  PhraseConfig next;
  bool configChanged;
  Command command;
  uint64_t commandUs;
  {
    CriticalGuard guard(_lock);
    configChanged = _pendingSet;
    next = _pending;
    _pendingSet = false;
    command = _command;
    commandUs = _commandUs;
    _command = Command::None;
  }
  if (configChanged) _active = next;
  switch (command) {
    case Command::Realign: doRealign(commandUs, false); break;
    case Command::RealignPhrase: doRealign(commandUs, true); break;
    case Command::Reset:
      _offsetBars = (int32_t)((_ticks ? _ticks - 1 : 0) / (24u * _beatsPerBar));
      _synced = true;
      break;
    default: break;
  }
  if (configChanged || command != Command::None) publish();
}

void PhraseTracker::publish()
{
  PhraseSnapshot s = {};
  uint32_t ticksPerBar = 24u * _beatsPerBar;
  uint32_t position = _ticks ? _ticks - 1 : 0;
  int32_t phraseBars = _active.phraseBars ? _active.phraseBars : 16;
  int32_t absBar = (int32_t)(position / ticksPerBar);
  int32_t bar = absBar - _offsetBars;
  int32_t barInPhrase = floorMod(bar, phraseBars);
  int32_t phrase = floorDiv(bar, phraseBars);
  int32_t warn = _active.warnBars < phraseBars ? _active.warnBars : 0;

  s.running = _running;
  s.counting = _running || (_external && !_stopped && _seenTick);
  s.synced = _synced;
  s.bar = bar;
  s.barInPhrase = barInPhrase;
  s.phraseBars = phraseBars;
  s.phrase = phrase < 0 ? 0 : phrase;
  s.sectionPhrases = _active.sectionPhrases;
  s.phraseInSection = _active.sectionPhrases ? floorMod(phrase, _active.sectionPhrases) : 0;
  s.barsLeft = phraseBars - barInPhrase;
  s.warning = warn && barInPhrase >= phraseBars - warn;
  s.beatInBar = (position / 24) % _beatsPerBar;
  s.beatsPerBar = _beatsPerBar;
  s.phraseStarts = _phraseStarts;
  s.nextPhraseTick = (uint32_t)(absBar + (phraseBars - barInPhrase)) * ticksPerBar;
  CriticalGuard guard(_lock);
  _snapshot = s;
}
