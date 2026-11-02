#include "haptics.h"

#include <Arduino.h>
#include <Preferences.h>

#include "haptic_patterns.h"

static constexpr uint32_t kSaveDelayMillis = 2000;
static constexpr uint32_t kTestGapMicros = 700000;
static const char *const kNamespace = "oximate";
static const char *const kKey = "haptic";

/*! Order of the test sequence: highest priority first */
static const Cue kTestOrder[] = {Cue::ClockLost,   Cue::PanicDone,  Cue::PhraseStart, Cue::PhraseEnding,
                                 Cue::Downbeat,    Cue::ClockLocked, Cue::Beat,       Cue::Kick,
                                 Cue::RightAnswer, Cue::WrongAnswer, Cue::LowBattery, Cue::TouchTick};

CueGroup Haptics::groupOf(Cue cue)
{
  switch (cue) {
    case Cue::Beat:
    case Cue::Downbeat:
    case Cue::CountIn:
    case Cue::CountInLast: return CueGroup::Metronome;
    case Cue::PhraseStart:
    case Cue::PhraseEnding: return CueGroup::Phrase;
    case Cue::ClockLost:
    case Cue::ClockLocked: return CueGroup::ClockAlerts;
    case Cue::TouchTick: return CueGroup::Touch;
    case Cue::PanicDone: return CueGroup::Panic;
    case Cue::Kick: return CueGroup::Kick;
    case Cue::RightAnswer:
    case Cue::WrongAnswer: return CueGroup::Answers;
    default: return CueGroup::Battery;
  }
}

void Haptics::begin(const Output &output, ClockEngine *clock)
{
  _output = output;
  _clock = clock;
  load();
  changed();
  _dirty = false; // loading isn't an edit
}

// ---------------------------------------------------------------------------- settings

void Haptics::changed()
{
  _leadMillis = _config.leadMillis;
  _kickNote = _config.kickNote;
  _kickChannel = _config.kickChannel;
  _kickOn = _config.outputs[(int)CueGroup::Kick] != CueOutput::Off;
  _dirty = true;
  _changedMillis = millis();
}

void Haptics::poll()
{
  if (_dirty && millis() - _changedMillis >= kSaveDelayMillis) {
    save();
    _dirty = false;
  }
}

void Haptics::load()
{
  Preferences prefs;
  if (!prefs.begin(kNamespace, true)) return;
  HapticConfig stored;
  if (prefs.getBytesLength(kKey) == sizeof(stored) && prefs.getBytes(kKey, &stored, sizeof(stored)) == sizeof(stored) &&
      stored.version == HapticConfig::kVersion) {
    _config = stored;
  }
  prefs.end();
}

void Haptics::save()
{
  Preferences prefs;
  if (prefs.begin(kNamespace, false)) {
    prefs.putBytes(kKey, &_config, sizeof(_config));
    prefs.end();
  }
}

// ---------------------------------------------------------------------------- player (UI thread)

void Haptics::setLevel(uint8_t level)
{
  if (level == _level) return; // every call is an I2C write: only on change
  _level = level;
  if (_output.vibrate) _output.vibrate(level, _output.ctx);
}

void Haptics::start(const Cues::Item &item, uint32_t nowMicros)
{
  CueOutput output = _config.outputs[(int)groupOf(item.cue)];
  if (output == CueOutput::Off) return;
  // A scheduled beat that comes due after Stop is not felt
  if ((item.cue == Cue::Beat || item.cue == Cue::Downbeat) && item.dueMicros && _clock &&
      !_clock->snapshot().running) {
    return;
  }
  if (output == CueOutput::Click || output == CueOutput::Both) {
    bool accent = item.cue == Cue::Downbeat || item.cue == Cue::PhraseStart || item.cue == Cue::CountInLast;
    if (_output.click) _output.click(accent, _output.ctx);
  }
  if (output == CueOutput::Click) return;

  const HapticPatterns::Pattern &pattern = HapticPatterns::kPatterns[(int)item.cue];
  if (_playing && pattern.priority < HapticPatterns::kPatterns[(int)_current].priority) {
    return; // a more important cue is playing
  }
  _playing = true;
  _current = item.cue;
  _segment = 0;
  _kickMillis = item.cue == Cue::Kick ? HapticPatterns::kickMillis(item.velocity) : 0;
  uint16_t length = _kickMillis ? _kickMillis : pattern.segments[0].millis;
  _segmentEnd = nowMicros + length * 1000u;
  setLevel(pattern.segments[0].level * _config.strength / 235);
}

void Haptics::checkClock()
{
  if (!_clock) return;
  ClockEngine::Snapshot snap = _clock->snapshot();
  bool following = snap.mode != ClockMode::Internal;
  bool lost = following && snap.lost;
  bool locked = following && snap.locked;
  if (lost && !_wasLost) Cues::post(Cue::ClockLost);
  if (locked && !_wasLocked && _wasLost) Cues::post(Cue::ClockLocked); // locked again after a loss
  if (lost) _wasLost = true;
  if (locked) _wasLost = false;
  _wasLocked = locked;
}

void Haptics::test()
{
  _testIndex = 0;
  _testNext = 0;
}

void Haptics::service(uint32_t nowMicros)
{
  checkClock();

  if (_testIndex >= 0 && (int32_t)(nowMicros - _testNext) >= 0) {
    Cues::Item item = {kTestOrder[_testIndex], 100, 0};
    CueOutput saved = _config.outputs[(int)groupOf(item.cue)];
    if (saved == CueOutput::Off) _config.outputs[(int)groupOf(item.cue)] = CueOutput::Vibrate; // test every pattern
    _playing = false;
    start(item, nowMicros);
    _config.outputs[(int)groupOf(item.cue)] = saved;
    _testNext = nowMicros + kTestGapMicros;
    if (++_testIndex >= (int)(sizeof(kTestOrder) / sizeof(kTestOrder[0]))) _testIndex = -1;
  }

  // New cues: now, or kept until they are due
  Cues::Item item;
  while (Cues::queue().take(item)) {
    if (item.dueMicros && (int32_t)(item.dueMicros - nowMicros) > 0) {
      if (_scheduledCount < kScheduled) _scheduled[_scheduledCount++] = item;
    } else {
      start(item, nowMicros);
    }
  }
  for (int i = 0; i < _scheduledCount;) {
    if ((int32_t)(_scheduled[i].dueMicros - nowMicros) <= 0) {
      Cues::Item due = _scheduled[i];
      _scheduled[i] = _scheduled[--_scheduledCount];
      start(due, nowMicros);
    } else {
      i++;
    }
  }

  // The playing pattern
  if (_playing && (int32_t)(nowMicros - _segmentEnd) >= 0) {
    const HapticPatterns::Pattern &pattern = HapticPatterns::kPatterns[(int)_current];
    if (++_segment >= pattern.count) {
      _playing = false;
      setLevel(0);
    } else {
      const HapticPatterns::Segment &segment = pattern.segments[_segment];
      _segmentEnd = nowMicros + segment.millis * 1000u;
      setLevel(segment.level * _config.strength / 235);
    }
  }
}

// ---------------------------------------------------------------------------- MIDI task side

void Haptics::clockListener(const ClockEvent &event, void *ctx)
{
  if (event.type != ClockEvent::Beat) return;
  Haptics *haptics = static_cast<Haptics *>(ctx);
  ClockEngine::Snapshot snap = haptics->_clock->snapshot();
  if (snap.bpm <= 0) return;
  uint8_t beatsPerBar = snap.beatsPerBar ? snap.beatsPerBar : 4;
  uint32_t beat = event.tick / 24;
  uint32_t now = micros();
  if (beat == 0) {
    Cues::post(Cue::Downbeat); // the first beat after Start: no earlier event scheduled it
  }
  // The next beat, one period ahead, started early so its felt peak lands on time
  uint32_t period = (uint32_t)(60e6f / snap.bpm);
  uint32_t lead = haptics->_leadMillis * 1000u;
  uint32_t due = now + (period > lead ? period - lead : 0);
  Cues::post((beat + 1) % beatsPerBar == 0 ? Cue::Downbeat : Cue::Beat, due ? due : 1);
}

void Haptics::kickObserver(const MidiMessage &msg, bool, void *ctx)
{
  Haptics *haptics = static_cast<Haptics *>(ctx);
  if (!haptics->_kickOn || !msg.isChannel()) return;
  if ((msg.status() & 0xF0) == 0x90 && msg.bytes[2] > 0 && msg.channel() == haptics->_kickChannel &&
      msg.bytes[1] == haptics->_kickNote) {
    Cues::post(Cue::Kick, 0, msg.bytes[2]);
  }
}
