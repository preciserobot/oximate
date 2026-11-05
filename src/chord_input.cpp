#include "chord_input.h"

#include <Arduino.h>
#include <string.h>

static constexpr uint32_t kPruneMillis = 10; // how often the arpeggio window is checked

void ChordInput::observer(const MidiMessage &msg, bool, void *ctx)
{
  static_cast<ChordInput *>(ctx)->observe(msg, millis());
}

bool ChordInput::passes(int input, uint8_t channel) const
{
  if (!(_filter.inputs & (1 << input))) return false;
  if (_filter.channel) return channel == _filter.channel;
  return !(_filter.skipDrums && channel == 10);
}

void ChordInput::observe(const MidiMessage &msg, uint32_t nowMillis)
{
  if (!msg.isChannel() || !isWired(msg.source)) return;
  int input = (int)msg.source;
  uint8_t channel = msg.channel(); // 1-16
  uint8_t type = msg.status() & 0xF0;
  uint32_t *held = _held[input][channel - 1];
  uint32_t *sustained = _sustained[input][channel - 1];
  bool counted = passes(input, channel);

  if (type == 0x90 && msg.bytes[2] > 0) {
    uint8_t note = msg.bytes[1] & 0x7F;
    set(held, note);
    reset(sustained, note); // struck again while the pedal holds it
  } else if (type == 0x80 || type == 0x90) {
    uint8_t note = msg.bytes[1] & 0x7F;
    if (!test(held, note)) return;
    reset(held, note);
    if (_pedal[input] & (1 << (channel - 1))) {
      set(sustained, note);
    }
    if (counted) _lastOff[note] = nowMillis ? nowMillis : 1;
  } else if (type == 0xB0) {
    uint8_t controller = msg.bytes[1];
    if (controller == 64) {
      if (msg.bytes[2] >= 64) {
        _pedal[input] |= 1 << (channel - 1);
        return; // nothing changes until notes are released
      }
      _pedal[input] &= ~(1 << (channel - 1));
      for (int note = 0; note < 128; note++) {
        if (test(sustained, note) && counted) _lastOff[note] = nowMillis ? nowMillis : 1;
      }
      memset(sustained, 0, sizeof(_sustained[0][0]));
    } else if (controller == 120 || controller == 123) { // all sound off / all notes off
      memset(held, 0, sizeof(_held[0][0]));
      memset(sustained, 0, sizeof(_sustained[0][0]));
    } else {
      return;
    }
  } else {
    return;
  }
  publish(nowMillis);
}

void ChordInput::clearInput(int input)
{
  memset(_held[input], 0, sizeof(_held[input]));
  memset(_sustained[input], 0, sizeof(_sustained[input]));
  _pedal[input] = 0;
}

void ChordInput::run(uint32_t nowMillis, bool usbConnected, uint32_t panics)
{
  bool filterSet, clear;
  ChordFilter filter;
  {
    CriticalGuard guard(_lock);
    filterSet = _filterSet;
    filter = _pendingFilter;
    clear = _clearRequested;
    _filterSet = _clearRequested = false;
  }
  bool changed = false;
  if (filterSet) {
    _filter = filter;
    changed = true;
  }
  if (clear || panics != _panics) { // a panic sent note-offs for everything
    _panics = panics;
    clearInput(0);
    clearInput(1);
    memset(_lastOff, 0, sizeof(_lastOff));
    changed = true;
  }
  if (_usbWasConnected && !usbConnected) { // its notes will never get their note-offs
    clearInput((int)MidiSource::Usb);
    changed = true;
  }
  _usbWasConnected = usbConnected;
  if (_filter.arpMillis && nowMillis - _lastPrune >= kPruneMillis) {
    _lastPrune = nowMillis;
    changed = true; // notes may have left the arpeggio window
  }
  if (changed) publish(nowMillis);
}

void ChordInput::publish(uint32_t nowMillis)
{
  uint32_t notes[4] = {0, 0, 0, 0};
  for (int input = 0; input < 2; input++) {
    for (int channel = 1; channel <= 16; channel++) {
      if (!passes(input, channel)) continue;
      for (int w = 0; w < 4; w++) {
        notes[w] |= _held[input][channel - 1][w];
        if (_filter.sustain) notes[w] |= _sustained[input][channel - 1][w];
      }
    }
  }
  if (_filter.arpMillis) {
    for (int note = 0; note < 128; note++) {
      if (_lastOff[note] && nowMillis - _lastOff[note] < _filter.arpMillis) set(notes, note);
    }
  }
  if (memcmp(notes, _published, sizeof(notes)) == 0) return;
  memcpy(_published, notes, sizeof(notes));

  NoteSet set = {{notes[0], notes[1], notes[2], notes[3]}, 0xFF, 0, ++_version};
  for (int note = 127; note >= 0; note--) {
    if (test(notes, note)) {
      set.lowest = note;
      set.count++;
    }
  }
  CriticalGuard guard(_lock);
  _snapshot = set;
}

void ChordInput::setFilter(const ChordFilter &filter)
{
  CriticalGuard guard(_lock);
  _pendingFilter = filter;
  _filterSet = true;
}

void ChordInput::requestClear()
{
  CriticalGuard guard(_lock);
  _clearRequested = true;
}

NoteSet ChordInput::snapshot() const
{
  CriticalGuard guard(_lock);
  return _snapshot;
}
