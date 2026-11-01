#include "panic.h"

#include <Arduino.h>

#include "clock_engine.h"
#include "usb_midi.h"

static constexpr uint32_t kStuckCheckMillis = 1000;
static constexpr uint32_t kSummaryMillis = 100;

void Panic::begin(MidiOutputs &outputs, NoteTracker &tracker, UsbMidi &usb, ClockEngine &clock)
{
  _outputs = &outputs;
  _tracker = &tracker;
  _usb = &usb;
  _clock = &clock;
}

void Panic::request(uint8_t level)
{
  CriticalGuard guard(_lock);
  if (level > _requested) _requested = level > 3 ? 3 : level;
}

void Panic::requestTestNote()
{
  CriticalGuard guard(_lock);
  _testRequested = true;
}

Panic::Summary Panic::summary() const
{
  CriticalGuard guard(_lock);
  return _summary;
}

void Panic::observe(const MidiMessage &msg)
{
  if (msg.source == MidiSource::Din && msg.status() == 0xFE) {
    _dinSensing = true;
    _lastSensingMillis = millis();
  }
}

void Panic::releaseFrom(Origin origin)
{
  int before = _tracker->heldCount(MidiOutput::Din) + _tracker->heldCount(MidiOutput::Usb);
  _tracker->release(*_outputs, MidiOutput::Din, &origin);
  _tracker->release(*_outputs, MidiOutput::Usb, &origin);
  int after = _tracker->heldCount(MidiOutput::Din) + _tracker->heldCount(MidiOutput::Usb);
  _autoReleased += before - after;
}

void Panic::startJob(uint8_t level)
{
  _level = level;
  for (int o = 0; o < 2; o++) {
    MidiOutput out = (MidiOutput)o;
    // Level 1: note-offs for what we know is held (and pedals up)
    _tracker->release(*_outputs, out);
    // Every panic also stops sequencers downstream (decided 2026-11-01)
    static const uint8_t stop = 0xFC;
    _outputs->send(out, {MidiSource::Din, 0, &stop, 1}, Origin::internal(kOwner));
    _cursor[o] = {0, level < 2 || !_outputs->available(out)};
  }
  _clock->stop(); // OxiMate's own clock and sequencers too
}

/*! Send as much of the level 2/3 stream to one output as its queue takes right now */
void Panic::feed(MidiOutput out)
{
  Cursor &cursor = _cursor[(int)out];
  uint16_t total = kAllOffMessages + (_level >= 3 ? kBruteMessages : 0);
  while (!cursor.done && _outputs->hasSpace(out, 3)) {
    uint8_t bytes[3];
    if (cursor.index < kAllOffMessages) {
      static const uint8_t kControllers[3] = {123, 120, 64}; // all notes off, all sound off, pedal up
      bytes[0] = 0xB0 | (cursor.index / 3);
      bytes[1] = kControllers[cursor.index % 3];
      bytes[2] = 0;
    } else {
      uint16_t i = cursor.index - kAllOffMessages;
      bytes[0] = 0x80 | (i / 128);
      bytes[1] = i % 128;
      bytes[2] = 0;
    }
    _outputs->send(out, {MidiSource::Din, 0, bytes, 3}, Origin::internal(kOwner));
    if (++cursor.index >= total) cursor.done = true;
  }
}

void Panic::run(uint32_t nowMillis)
{
  uint8_t requested;
  bool testNote;
  {
    CriticalGuard guard(_lock);
    requested = _requested;
    _requested = 0;
    testNote = _testRequested;
    _testRequested = false;
  }
  if (testNote && !_testOffAt) {
    uint8_t on[3] = {0x90, 60, 100};
    _outputs->send(MidiOutput::Din, {MidiSource::Din, 0, on, 3}, Origin::internal(kOwner));
    _testOffAt = nowMillis + 500;
  } else if (_testOffAt && (int32_t)(nowMillis - _testOffAt) >= 0) {
    uint8_t off[3] = {0x80, 60, 0};
    _outputs->send(MidiOutput::Din, {MidiSource::Din, 0, off, 3}, Origin::internal(kOwner));
    _testOffAt = 0;
  }
  if (requested) {
    startJob(requested > _level ? requested : _level);
  }
  bool wasRunning = _level != 0;
  if (_level) {
    for (int o = 0; o < 2; o++) {
      if (!_outputs->available((MidiOutput)o)) _cursor[o].done = true;
      feed((MidiOutput)o);
    }
    if (_cursor[0].done && _cursor[1].done) {
      _tracker->clear(MidiOutput::Din);
      _tracker->clear(MidiOutput::Usb);
      _panics++;
      _level = 0;
    }
  }

  // USB device unplugged: it can't receive note-offs any more, and its notes on DIN must end
  bool usbConnected = _usb->connected();
  if (_usbWasConnected && !usbConnected) {
    _tracker->clear(MidiOutput::Usb);
    releaseFrom(Origin::fromInput(MidiSource::Usb));
  }
  _usbWasConnected = usbConnected;

  // DIN sender stopped its Active Sensing: release its notes, as the MIDI spec asks receivers to
  if (_dinSensing && nowMillis - _lastSensingMillis > kActiveSensingMillis) {
    _dinSensing = false;
    releaseFrom(Origin::fromInput(MidiSource::Din));
  }

  // Notes held longer than kStuckMillis are released
  if (nowMillis - _lastStuckCheck >= kStuckCheckMillis) {
    _lastStuckCheck = nowMillis;
    MidiOutput out;
    NoteTracker::HeldNote stuck;
    for (int i = 0; i < 16 && _tracker->findStuck(nowMillis, kStuckMillis, out, stuck); i++) {
      uint8_t off[3] = {(uint8_t)(0x80 | stuck.channel), stuck.note, 0};
      _outputs->send(out, {MidiSource::Din, 0, off, 3}, stuck.origin);
      _tracker->forget(out, stuck.channel, stuck.note); // also when a pedal still sustains it
      _autoReleased++;
    }
  }

  if (nowMillis - _lastSummary >= kSummaryMillis || _level || wasRunning) {
    _lastSummary = nowMillis;
    updateSummary();
  }
}

void Panic::updateSummary()
{
  Summary s;
  for (int o = 0; o < 2; o++) {
    s.held[o] = _tracker->heldCount((MidiOutput)o);
    s.sustain[o] = __builtin_popcount(_tracker->sustainMask((MidiOutput)o));
  }
  s.panics = _panics;
  s.autoReleased = _autoReleased;
  s.active = _level != 0;
  s.level = _level;
  uint32_t total = kAllOffMessages + (_level >= 3 ? kBruteMessages : 0);
  uint32_t sent = 0;
  int outputs = 0;
  for (int o = 0; o < 2; o++) {
    if (_outputs->available((MidiOutput)o)) {
      sent += _cursor[o].done ? total : _cursor[o].index;
      outputs++;
    }
  }
  s.progress = _level && outputs ? (uint8_t)(sent * 100 / (total * outputs)) : 0;
  CriticalGuard guard(_lock);
  _summary = s;
}
