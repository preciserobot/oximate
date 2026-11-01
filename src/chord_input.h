#pragma once

#include "midi_message.h"
#include "sync.h"

/*! Which input notes count for the chord detector */
struct ChordFilter
{
  uint8_t inputs = 3;      ///< bit 0 USB, bit 1 DIN
  uint8_t channel = 0;     ///< 0 = all, 1-16
  bool skipDrums = true;   ///< with channel 0: ignore channel 10 (decided 2026-11-01)
  bool sustain = false;    ///< notes held by the sustain pedal count
  uint32_t arpMillis = 0;  ///< notes stay in the set this long after their note-off (0 = off)
};

/*! The notes currently held at the inputs (after the filter), for the chord detector */
struct NoteSet
{
  uint32_t notes[4];  ///< 128-bit set
  uint8_t lowest;     ///< 0xFF = empty
  uint8_t count;
  uint32_t version;   ///< increments on every change
};

/*! Stage 21: an input observer in the MIDI task that keeps the exact set of notes held at the
 *  inputs (velocity-0 note-ons, sustain pedal, all-notes-off), so the UI never misses a note-off
 *  even when its event ring overflows. Only bit operations here; naming chords happens on the UI
 *  side from snapshot(). */
class ChordInput
{
public:
  static void observer(const MidiMessage &msg, bool forwarded, void *ctx);

  // MIDI task
  void observe(const MidiMessage &msg, uint32_t nowMillis);
  /*! Every task loop: settings from the UI, arpeggio window expiry, and clearing when the USB
   *  device goes away or a panic was completed */
  void run(uint32_t nowMillis, bool usbConnected, uint32_t panics);

  // UI
  void setFilter(const ChordFilter &filter);
  void requestClear(); ///< forget all held notes (manual unstick)
  NoteSet snapshot() const;

private:
  bool passes(int input, uint8_t channel) const;
  void clearInput(int input);
  void publish(uint32_t nowMillis);
  static bool test(const uint32_t bits[4], uint8_t note) { return bits[note >> 5] & (1u << (note & 31)); }
  static void set(uint32_t bits[4], uint8_t note) { bits[note >> 5] |= 1u << (note & 31); }
  static void reset(uint32_t bits[4], uint8_t note) { bits[note >> 5] &= ~(1u << (note & 31)); }

  // MIDI task state
  uint32_t _held[2][16][4] = {};
  uint32_t _sustained[2][16][4] = {}; ///< released while the pedal was down
  uint16_t _pedal[2] = {0, 0};
  uint32_t _lastOff[128] = {};        ///< when a counted note was last released (arpeggio window)
  ChordFilter _filter;
  bool _usbWasConnected = false;
  uint32_t _panics = 0;
  uint32_t _lastPrune = 0;
  uint32_t _version = 0;
  uint32_t _published[4] = {};

  // Handoff
  mutable CriticalSection _lock;
  ChordFilter _pendingFilter;
  bool _filterSet = false;
  bool _clearRequested = false;
  NoteSet _snapshot = {{0, 0, 0, 0}, 0xFF, 0, 0};
};
