#include "note_tracker.h"

#include <Arduino.h>
#include <string.h>

NoteTracker::Slot *NoteTracker::findSlot(State &s, uint8_t channel, uint8_t note)
{
  for (Slot &slot : s.slots) {
    if (slot.channel == channel && slot.note == note) return &slot;
  }
  return nullptr;
}

const NoteTracker::Slot *NoteTracker::findSlot(const State &s, uint8_t channel, uint8_t note) const
{
  for (const Slot &slot : s.slots) {
    if (slot.channel == channel && slot.note == note) return &slot;
  }
  return nullptr;
}

void NoteTracker::freeSlot(State &s, uint8_t channel, uint8_t note)
{
  if (Slot *slot = findSlot(s, channel, note)) {
    slot->channel = kFree;
  }
}

void NoteTracker::clearChannel(State &s, uint8_t channel)
{
  memset(s.held[channel], 0, sizeof(s.held[channel]));
  memset(s.sustained[channel], 0, sizeof(s.sustained[channel]));
  for (Slot &slot : s.slots) {
    if (slot.channel == channel) slot.channel = kFree;
  }
}

void NoteTracker::sent(MidiOutput out, const MidiMessage &msg, Origin origin, uint32_t nowMillis)
{
  if (!msg.isChannel()) {
    return;
  }
  State &s = _state[(int)out];
  uint8_t channel = msg.status() & 0x0F;
  uint8_t type = msg.status() & 0xF0;
  uint8_t note = msg.bytes[1] & 0x7F;

  if (type == 0x90 && msg.bytes[2] > 0) {
    set(s.held[channel], note);
    reset(s.sustained[channel], note); // struck again while sustained
    Slot *slot = findSlot(s, channel, note);
    if (!slot) {
      for (Slot &candidate : s.slots) {
        if (candidate.channel == kFree) {
          slot = &candidate;
          break;
        }
      }
    }
    if (slot) { // when all slots are taken only age and origin are lost; the bitsets stay correct
      slot->channel = channel;
      slot->note = note;
      slot->origin = origin;
      slot->startMillis = nowMillis ? nowMillis : millis();
    }
  } else if (type == 0x80 || type == 0x90) {
    if (!test(s.held[channel], note)) {
      return;
    }
    reset(s.held[channel], note);
    if (s.pedal & (1 << channel)) {
      set(s.sustained[channel], note); // keeps its slot until the pedal goes up
    } else {
      freeSlot(s, channel, note);
    }
  } else if (type == 0xB0) {
    uint8_t controller = msg.bytes[1];
    uint8_t value = msg.bytes[2];
    if (controller == 64) {
      if (value >= 64) {
        s.pedal |= (1 << channel);
      } else {
        s.pedal &= ~(1 << channel);
        for (int n = 0; n < 128; n++) {
          if (test(s.sustained[channel], n)) {
            reset(s.sustained[channel], n);
            freeSlot(s, channel, n);
          }
        }
      }
    } else if (controller == 120 || controller == 123) { // all sound off / all notes off
      clearChannel(s, channel);
    }
  }
}

bool NoteTracker::isHeld(MidiOutput out, uint8_t channel, uint8_t note) const
{
  const State &s = _state[(int)out];
  return test(s.held[channel], note) || test(s.sustained[channel], note);
}

uint16_t NoteTracker::heldCount(MidiOutput out) const
{
  const State &s = _state[(int)out];
  uint16_t count = 0;
  for (int ch = 0; ch < 16; ch++) {
    for (int w = 0; w < 4; w++) {
      count += __builtin_popcount(s.held[ch][w] | s.sustained[ch][w]);
    }
  }
  return count;
}

uint16_t NoteTracker::sustainMask(MidiOutput out) const { return _state[(int)out].pedal; }

int NoteTracker::collect(MidiOutput out, HeldNote *notes, int maxNotes, const Origin *only) const
{
  const State &s = _state[(int)out];
  int count = 0;
  for (uint8_t ch = 0; ch < 16; ch++) {
    for (int n = 0; n < 128 && count < maxNotes; n++) {
      if (!test(s.held[ch], n) && !test(s.sustained[ch], n)) continue;
      const Slot *slot = findSlot(s, ch, n);
      Origin origin = slot ? slot->origin : Origin{Origin::Internal, 0xFF}; // 0xFF: unknown
      if (only && !(origin == *only)) continue;
      notes[count++] = {ch, (uint8_t)n, origin, slot ? slot->startMillis : 0};
    }
  }
  return count;
}

void NoteTracker::release(MidiOutputs &outputs, MidiOutput out, const Origin *only)
{
  // Note by note, reading the state afresh each time: a note-off only changes its own note (no
  // copy of every held note: that took 16 KB of internal RAM, which BLE needs - stage 17)
  State &s = _state[(int)out];
  for (uint8_t ch = 0; ch < 16; ch++) {
    for (int n = 0; n < 128; n++) {
      if (!test(s.held[ch], n) && !test(s.sustained[ch], n)) continue;
      const Slot *slot = findSlot(s, ch, n);
      Origin origin = slot ? slot->origin : Origin{Origin::Internal, 0xFF}; // 0xFF: unknown
      if (only && !(origin == *only)) continue;
      uint8_t bytes[3] = {(uint8_t)(0x80 | ch), (uint8_t)n, 0};
      MidiMessage off = {MidiSource::Din, 0, bytes, 3};
      outputs.send(out, off, origin);
      // A sustained note stays sustained after its note-off; it is dropped here
      reset(s.sustained[ch], n);
      freeSlot(s, ch, n);
    }
  }
  if (!only) {
    uint16_t pedal = _state[(int)out].pedal;
    for (uint8_t ch = 0; ch < 16; ch++) {
      if (pedal & (1 << ch)) {
        uint8_t bytes[3] = {(uint8_t)(0xB0 | ch), 64, 0};
        MidiMessage up = {MidiSource::Din, 0, bytes, 3};
        outputs.send(out, up, Origin::internal(16));
      }
    }
  }
}

void NoteTracker::clear(MidiOutput out) { _state[(int)out] = State(); }

void NoteTracker::forget(MidiOutput out, uint8_t channel, uint8_t note)
{
  State &s = _state[(int)out];
  reset(s.held[channel], note);
  reset(s.sustained[channel], note);
  freeSlot(s, channel, note);
}

bool NoteTracker::findStuck(uint32_t nowMillis, uint32_t thresholdMillis, MidiOutput &out, HeldNote &stuck) const
{
  bool found = false;
  for (int o = 0; o < kOutputs; o++) {
    const State &s = _state[o];
    for (const Slot &slot : s.slots) {
      if (slot.channel == kFree) continue;
      uint32_t age = nowMillis - slot.startMillis;
      if (age >= thresholdMillis && (!found || slot.startMillis < stuck.startMillis)) {
        found = true;
        out = (MidiOutput)o;
        stuck = {slot.channel, slot.note, slot.origin, slot.startMillis};
      }
    }
  }
  return found;
}
