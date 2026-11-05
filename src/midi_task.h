#pragma once

#include <atomic>
#include "midi_message.h"
#include "midi_outputs.h"

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

/*! Single-producer (UI) / single-consumer (MIDI task) queue of messages to send, so pages can
 *  play notes without touching MidiOutputs from the wrong core. Messages up to kMaxBytes. */
class SendQueue
{
public:
  static constexpr size_t kLength = 64;
  static constexpr uint8_t kMaxBytes = 16;

  struct Item
  {
    MidiOutput out;
    Origin origin;
    uint8_t length;
    uint8_t bytes[kMaxBytes];
  };

  bool push(MidiOutput out, const uint8_t *bytes, uint8_t length, Origin origin); ///< UI; false if full
  void drain(MidiOutputs &outputs);                                               ///< MIDI task

private:
  Item _items[kLength];
  std::atomic<size_t> _head{0};
  std::atomic<size_t> _tail{0};
};

class UsbMidi;
class DinMidi;
class ClockEngine;

/*! The MIDI task: owns USB, DIN, router and outputs, and runs every millisecond on its
 *  own core, so screen redraws can't delay MIDI. Everything it learns goes to the UI
 *  through events(). */
namespace MidiTask
{
constexpr int kDefaultCore = 0; ///< with the radios off: core 0 is otherwise idle
constexpr int kRadioCore = 1;   ///< BLE / Wi-Fi stacks run on core 0 at priorities 18-23 (stage 17)
constexpr int kPriority = 10;   ///< above loopTask (1), below the radio stacks (18-23)
constexpr uint32_t kStackBytes = 8192;

void start(UsbMidi &usb, DinMidi &din, ClockEngine &clock, MidiOutputs &outputs, int core = kDefaultCore);
int core(); ///< the core the task was started on
/*! UI side: send a message from the MIDI task (sent within ~1 ms); false if the queue is full */
bool send(MidiOutput out, const uint8_t *bytes, uint8_t length, Origin origin);
/*! Extra work for every task loop (router hand-over, panic, statistics); set before start() */
void setLoopHook(void (*hook)(uint32_t nowMillis));
EventRing &events();
} // namespace MidiTask
