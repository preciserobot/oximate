#pragma once

#include <atomic>
#include "midi_outputs.h"

/*! Notes that OxiMate generates itself (Gen slots, the looper, ...) for the built-in speaker.
 *  A MidiOutputs listener in the MIDI task copies note-ons and note-offs sent to DIN by internal
 *  generators into a small queue; the UI plays them on SpeakerSynth while the Synth page's output
 *  is "Speaker". Sources with their own speaker path are skipped: the synth's settings (6), the
 *  drum machine (5, its own hit queue), the ear trainer (7, plays the speaker directly), the
 *  clock (8) and panic (16). */
class LocalSoundTap
{
public:
  struct Note
  {
    uint8_t channel; ///< 1-16
    uint8_t note;
    uint8_t velocity; ///< 0 = note-off
  };

  static constexpr size_t kLength = 64;

  void begin(MidiOutputs &outputs) { outputs.addListener(onSent, this); }

  /*! UI thread: the next generated note, false when none */
  bool take(Note &note)
  {
    size_t tail = _tail.load(std::memory_order_relaxed);
    if (tail == _head.load(std::memory_order_acquire)) return false;
    note = _notes[tail];
    _tail.store((tail + 1) % kLength, std::memory_order_release);
    return true;
  }

  static bool wanted(Origin origin)
  {
    if (origin.kind != Origin::Internal) return false;
    switch (origin.owner) {
      case 5: case 6: case 7: case 8: case 16: return false;
      default: return true;
    }
  }

private:
  static void onSent(MidiOutput out, const MidiMessage &msg, Origin origin, void *ctx)
  {
    if (out != MidiOutput::Din || !wanted(origin) || !msg.isChannel()) return;
    uint8_t type = msg.status() & 0xF0;
    if (type != 0x90 && type != 0x80) return;
    LocalSoundTap *tap = static_cast<LocalSoundTap *>(ctx);
    size_t head = tap->_head.load(std::memory_order_relaxed);
    size_t next = (head + 1) % kLength;
    if (next == tap->_tail.load(std::memory_order_acquire)) return; // full: drop
    tap->_notes[head] = {msg.channel(), msg.bytes[1], type == 0x90 ? msg.bytes[2] : (uint8_t)0};
    tap->_head.store(next, std::memory_order_release);
  }

  Note _notes[kLength];
  std::atomic<size_t> _head{0};
  std::atomic<size_t> _tail{0};
};
