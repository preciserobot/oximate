#include "drum_machine.h"

#include <Arduino.h>
#include <Preferences.h>

#include "input_observers.h"
#include "note_tracker.h"
#include "platform_time.h"

static const char *const kNamespace = "oximate";
static const char *const kKey = "drums";
static constexpr uint8_t kAccentFromPadVelocity = 110; ///< pad hits this hard record as accents

void DrumMachine::begin(MidiOutputs &outputs, ClockEngine &clock)
{
  _outputs = &outputs;
  _clock = &clock;
  load();
  clock.addListener(onClock, this);
  InputHooks::addInterceptor(onInput, this);
}

// ---------------------------------------------------------------------------- UI side: editing

DrumStep DrumMachine::step(int pattern, int lane, int step) const
{
  CriticalGuard guard(_lock);
  const DrumPattern &p = _bank.patterns[pattern];
  uint32_t bit = 1u << step;
  if (!(p.on[lane] & bit)) return DrumStep::Off;
  return (p.accent[lane] & bit) ? DrumStep::Accent : DrumStep::On;
}

void DrumMachine::setStep(int pattern, int lane, int step, DrumStep state)
{
  {
    CriticalGuard guard(_lock);
    DrumPattern &p = _bank.patterns[pattern];
    uint32_t bit = 1u << step;
    p.on[lane] = state == DrumStep::Off ? p.on[lane] & ~bit : p.on[lane] | bit;
    p.accent[lane] = state == DrumStep::Accent ? p.accent[lane] | bit : p.accent[lane] & ~bit;
  }
  changed();
}

void DrumMachine::setLane(int lane, const DrumLane &settings)
{
  {
    CriticalGuard guard(_lock);
    _bank.lanes[lane] = settings;
  }
  changed();
}

void DrumMachine::setLength(int pattern, uint8_t length)
{
  if (length < 1) length = 1;
  if (length > kSteps) length = kSteps;
  {
    CriticalGuard guard(_lock);
    _bank.patterns[pattern].length = length;
  }
  changed();
}

void DrumMachine::clearPattern(int pattern)
{
  {
    CriticalGuard guard(_lock);
    uint8_t length = _bank.patterns[pattern].length;
    _bank.patterns[pattern] = DrumPattern();
    _bank.patterns[pattern].length = length;
  }
  changed();
}

void DrumMachine::setPadChannel(uint8_t channel)
{
  _bank.padChannel = channel > 16 ? 0 : channel;
  changed();
}

void DrumMachine::setUsbOut(bool on)
{
  _bank.usbOut = on;
  changed();
}

void DrumMachine::setChain(const uint8_t *patterns, uint8_t length)
{
  {
    CriticalGuard guard(_lock);
    if (length > kPatterns) length = kPatterns;
    for (int i = 0; i < length; i++) _bank.chain[i] = patterns[i] % kPatterns;
    _bank.chainLength = length;
  }
  if (_chainIndex >= length) _chainIndex = 0;
  changed();
}

void DrumMachine::changed()
{
  _dirty = true;
  _changedMillis = millis();
}

void DrumMachine::poll()
{
  // A flash write stalls both cores for a moment, so it waits until playback stops
  if (_dirty && !_playing && millis() - _changedMillis >= kSaveDelayMillis) {
    save();
    _dirty = false;
  }
}

void DrumMachine::load()
{
  Preferences prefs;
  if (!prefs.begin(kNamespace, true)) return;
  DrumBank stored;
  if (prefs.getBytesLength(kKey) == sizeof(stored) && prefs.getBytes(kKey, &stored, sizeof(stored)) == sizeof(stored) &&
      stored.version == DrumBank::kVersion) {
    _bank = stored;
  }
  prefs.end();
}

void DrumMachine::save()
{
  DrumBank copy;
  {
    CriticalGuard guard(_lock);
    copy = _bank;
  }
  Preferences prefs;
  if (prefs.begin(kNamespace, false)) {
    prefs.putBytes(kKey, &copy, sizeof(copy));
    prefs.end();
  }
}

// ---------------------------------------------------------------------------- UI side: playback

void DrumMachine::queuePattern(int pattern)
{
  pattern %= kPatterns;
  if (_playing) {
    _queued = pattern;
  } else {
    _pattern = pattern;
    _queued = -1;
  }
}

void DrumMachine::setEnabled(bool on)
{
  _enabled = on;
}

void DrumMachine::audition(int lane) { _auditionLane = lane; }

bool DrumMachine::takeHit(uint8_t &note, uint8_t &velocity)
{
  int tail = _hitTail.load(std::memory_order_relaxed);
  if (tail == _hitHead.load(std::memory_order_acquire)) return false;
  note = _hitNotes[tail];
  velocity = _hitVelocities[tail];
  _hitTail.store((tail + 1) % kHitQueue, std::memory_order_release);
  return true;
}

// ---------------------------------------------------------------------------- MIDI task side

void DrumMachine::pushHit(uint8_t note, uint8_t velocity)
{
  _hits++;
  int head = _hitHead.load(std::memory_order_relaxed);
  int next = (head + 1) % kHitQueue;
  if (next == _hitTail.load(std::memory_order_acquire)) return; // the UI isn't reading: drop
  _hitNotes[head] = note;
  _hitVelocities[head] = velocity;
  _hitHead.store(next, std::memory_order_release);
}

void DrumMachine::send(const uint8_t *bytes, uint16_t length)
{
  MidiMessage msg = {MidiSource::Din, 0, bytes, length};
  _outputs->send(MidiOutput::Din, msg, Origin::internal(kOwner));
  if (_bank.usbOut && _outputs->available(MidiOutput::Usb)) {
    _outputs->send(MidiOutput::Usb, msg, Origin::internal(kOwner));
  }
}

void DrumMachine::noteOffCallback(void *ctx, uint32_t note, uint64_t)
{
  DrumMachine *drums = static_cast<DrumMachine *>(ctx);
  uint8_t off[3] = {(uint8_t)(0x80 | (drums->_bank.channel - 1)), (uint8_t)note, 0};
  drums->send(off, 3);
}

/*! Note-on, and a note-off half a step later (the GM kit ignores it, external drums need it) */
void DrumMachine::trigger(uint8_t note, uint8_t velocity, uint64_t timeUs, bool scheduleOff)
{
  uint8_t on[3] = {(uint8_t)(0x90 | (_bank.channel - 1)), note, velocity};
  send(on, 3);
  pushHit(note, velocity);
  if (scheduleOff && !_clock->schedule(timeUs + _stepUs / 2, noteOffCallback, this, note)) {
    noteOffCallback(this, note, timeUs); // scheduler full: end it now rather than never
  }
}

bool DrumMachine::laneAudible(int lane) const
{
  bool anySolo = false;
  for (const DrumLane &l : _bank.lanes) anySolo = anySolo || l.solo;
  const DrumLane &l = _bank.lanes[lane];
  return !l.muted && (!anySolo || l.solo);
}

void DrumMachine::playStep(uint64_t timeUs)
{
  int position = _position;
  const DrumPattern &p = _bank.patterns[_pattern];
  uint32_t bit = 1u << position;
  for (int lane = 0; lane < kLanes; lane++) {
    uint32_t on, accent;
    {
      CriticalGuard guard(_lock);
      on = p.on[lane];
      accent = p.accent[lane];
    }
    if (!(on & bit) || !laneAudible(lane)) continue;
    const DrumLane &l = _bank.lanes[lane];
    trigger(l.note, (accent & bit) ? l.accentVelocity : l.velocity, timeUs, true);
  }
}

/*! Next step; at the end of the pattern a queued pattern or the next chain entry takes over */
void DrumMachine::advance()
{
  int next = _position + 1;
  if (next < _bank.patterns[_pattern].length) {
    _position = next;
    return;
  }
  if (_queued >= 0) {
    _pattern = (int)_queued;
    _queued = -1;
  } else if (_bank.chainLength) {
    _chainIndex = (_chainIndex + 1) % _bank.chainLength;
    _pattern = _bank.chain[_chainIndex];
  }
  _position = 0;
}

void DrumMachine::releaseAll()
{
  Origin origin = Origin::internal(kOwner);
  _outputs->tracker().release(*_outputs, MidiOutput::Din, &origin);
  _outputs->tracker().release(*_outputs, MidiOutput::Usb, &origin);
}

void DrumMachine::onClock(const ClockEvent &event, void *ctx)
{
  DrumMachine *drums = static_cast<DrumMachine *>(ctx);
  switch (event.type) {
    case ClockEvent::Start:
      drums->_playing = true;
      drums->_position = -1; // the first Step plays step 1
      if (drums->_queued >= 0) {
        drums->_pattern = (int)drums->_queued;
        drums->_queued = -1;
      } else if (drums->_bank.chainLength) {
        drums->_chainIndex = 0;
        drums->_pattern = drums->_bank.chain[0];
      }
      break;
    case ClockEvent::Continue:
      drums->_playing = true;
      break;
    case ClockEvent::Stop:
      drums->_playing = false;
      drums->releaseAll();
      break;
    case ClockEvent::Locate: {
      int length = drums->_bank.patterns[drums->_pattern].length;
      drums->_position = (int)((event.tick / 6) % length) - 1;
      break;
    }
    case ClockEvent::Step: {
      if (!drums->_playing) break;
      ClockEngine::Snapshot snap = drums->_clock->snapshot();
      if (snap.bpm > 0) drums->_stepUs = (uint32_t)(60e6f / snap.bpm / 4);
      drums->advance();
      drums->_lastStepUs = event.timeUs;
      if (drums->_enabled) drums->playStep(event.timeUs);
      break;
    }
    default:
      break;
  }
}

/*! Pad controller: notes on the pad channel that match a lane play it (and are consumed) */
bool DrumMachine::onInput(const MidiMessage &msg, void *ctx)
{
  DrumMachine *drums = static_cast<DrumMachine *>(ctx);
  const DrumBank &bank = drums->_bank;
  if (!bank.padChannel || !msg.isChannel() || msg.channel() != bank.padChannel || msg.length < 3) return false;
  uint8_t type = msg.status() & 0xF0;
  if (type != 0x90 && type != 0x80) return false;
  uint8_t note = msg.bytes[1], velocity = msg.bytes[2];
  int lane = -1;
  for (int i = 0; i < kLanes && lane < 0; i++) {
    if (bank.lanes[i].note == note) lane = i;
  }
  if (lane < 0) return false;

  if (type == 0x80 || velocity == 0) {
    uint8_t off[3] = {(uint8_t)(0x80 | (bank.channel - 1)), note, 0};
    drums->send(off, 3);
    return true;
  }
  drums->trigger(note, velocity, nowMicros64(), false); // the pad's own note-off ends it

  if (drums->_recording && drums->_playing && drums->_position >= 0) {
    // Quantise to the nearest step: early in a step counts for this one, late for the next
    int step = drums->_position;
    uint64_t sinceStep = nowMicros64() - drums->_lastStepUs;
    if (sinceStep > drums->_stepUs / 2) step = (step + 1) % bank.patterns[drums->_pattern].length;
    uint32_t bit = 1u << step;
    {
      CriticalGuard guard(drums->_lock);
      DrumPattern &p = drums->_bank.patterns[drums->_pattern];
      p.on[lane] |= bit;
      if (velocity >= kAccentFromPadVelocity) p.accent[lane] |= bit;
    }
    drums->changed();
  }
  return true;
}

void DrumMachine::run(uint32_t)
{
  int lane = _auditionLane.exchange(-1);
  if (lane >= 0 && lane < kLanes) {
    trigger(_bank.lanes[lane].note, _bank.lanes[lane].velocity, nowMicros64(), true);
  }
}
