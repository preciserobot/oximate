#pragma once

#include "midi_message.h"
#include "midi_outputs.h"

/*! Held and sustained notes per output and channel, based on what was actually sent
 *  (after routing and channel remapping). Used for panic and automatic release.
 *  Runs in the MIDI task only; the UI will read snapshots (stage 16). */
class NoteTracker
{
public:
  static constexpr int kSlots = 128; ///< notes with known age and origin, per output

  struct HeldNote
  {
    uint8_t channel; ///< 0-15
    uint8_t note;
    Origin origin;
    uint32_t startMillis;
  };

  /*! Record a message sent to an output (called by MidiOutputs only) */
  void sent(MidiOutput out, const MidiMessage &msg, Origin origin, uint32_t nowMillis = 0);

  bool isHeld(MidiOutput out, uint8_t channel, uint8_t note) const; ///< held or sustained; channel 0-15
  uint16_t heldCount(MidiOutput out) const;                          ///< held + sustained
  uint16_t sustainMask(MidiOutput out) const;                        ///< bit n = pedal down on channel n

  /*! Copy held notes (optionally only those from one origin) into `notes`; returns the count */
  int collect(MidiOutput out, HeldNote *notes, int maxNotes, const Origin *only = nullptr) const;

  /*! Send note-offs for held notes (and CC64 = 0 where the pedal is down, when releasing
   *  everything) through `outputs`, which also clears them here */
  void release(MidiOutputs &outputs, MidiOutput out, const Origin *only = nullptr);

  void clear(MidiOutput out); ///< forget without sending (e.g. the USB device is gone)
  void forget(MidiOutput out, uint8_t channel, uint8_t note); ///< forget one note without sending

  /*! Oldest held note older than thresholdMillis */
  bool findStuck(uint32_t nowMillis, uint32_t thresholdMillis, MidiOutput &out, HeldNote &stuck) const;

private:
  struct Slot
  {
    uint8_t channel = kFree;
    uint8_t note = 0;
    Origin origin = {Origin::Internal, 0};
    uint32_t startMillis = 0;
  };
  static constexpr uint8_t kFree = 0xFF;

  struct State
  {
    uint32_t held[16][4] = {};      // 128 bits per channel
    uint32_t sustained[16][4] = {}; // released while the pedal was down
    uint16_t pedal = 0;
    Slot slots[kSlots];
  };

  static bool test(const uint32_t bits[4], uint8_t note) { return bits[note >> 5] & (1u << (note & 31)); }
  static void set(uint32_t bits[4], uint8_t note) { bits[note >> 5] |= (1u << (note & 31)); }
  static void reset(uint32_t bits[4], uint8_t note) { bits[note >> 5] &= ~(1u << (note & 31)); }

  Slot *findSlot(State &s, uint8_t channel, uint8_t note);
  const Slot *findSlot(const State &s, uint8_t channel, uint8_t note) const;
  void freeSlot(State &s, uint8_t channel, uint8_t note);
  void clearChannel(State &s, uint8_t channel);

  State _state[2];
};
