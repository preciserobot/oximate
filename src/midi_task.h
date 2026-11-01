#pragma once

#include <atomic>
#include "midi_message.h"

/*! A routed message as seen by the UI (monitor, LEDs, header).
 *  SysEx keeps only its first bytes plus the total length. */
struct MidiEvent
{
  static constexpr uint8_t kMaxBytes = 16;

  uint32_t timeMicros;
  MidiSource source;
  uint8_t cable;
  uint8_t count;    ///< bytes stored in `bytes`
  bool forwarded;   ///< false if the router dropped it
  uint16_t length;  ///< full message length
  uint8_t bytes[kMaxBytes];

  /*! View as a MidiMessage (truncated for long SysEx); valid while the event lives */
  MidiMessage message() const { return {source, cable, bytes, count, timeMicros}; }
};

/*! Single-producer (MIDI task) / single-consumer (UI) ring of MidiEvents.
 *  When full, new events are dropped and counted. */
class EventRing
{
public:
  static constexpr size_t kLength = 256;

  void push(const MidiMessage &msg, bool forwarded);
  bool pop(MidiEvent &event);
  uint32_t dropped() const { return _dropped; }

private:
  MidiEvent _events[kLength];
  std::atomic<size_t> _head{0}; // written by the producer
  std::atomic<size_t> _tail{0}; // written by the consumer
  std::atomic<uint32_t> _dropped{0};
};

class UsbMidi;
class DinMidi;
class ClockEngine;

/*! The MIDI task: owns USB, DIN, router and outputs, and runs every millisecond on its
 *  own core, so screen redraws can't delay MIDI. Everything it learns goes to the UI
 *  through events(). */
namespace MidiTask
{
constexpr int kCore = 0;      ///< see README "Shared infrastructure" (radio stacks may need core 1)
constexpr int kPriority = 10; ///< above loopTask (1), below the radio stacks (18-23)
constexpr uint32_t kStackBytes = 8192;

void start(UsbMidi &usb, DinMidi &din, ClockEngine &clock);
/*! Extra work for every task loop (router hand-over, panic, statistics); set before start() */
void setLoopHook(void (*hook)(uint32_t nowMillis));
EventRing &events();
} // namespace MidiTask
