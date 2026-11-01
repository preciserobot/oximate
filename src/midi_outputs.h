#pragma once

#include "midi_message.h"

class DinMidi;
class UsbMidi;
class NoteTracker;

enum class MidiOutput : uint8_t
{
  Din = 0, ///< DIN OUT of the Unit MIDI (+ SAM2695)
  Usb = 1, ///< USB device on the host module
};

/*! Who caused a sent message: an input passed through the router, or an internal generator */
struct Origin
{
  enum Kind : uint8_t
  {
    UsbIn,
    DinIn,
    Internal,
  };
  Kind kind;
  uint8_t owner; ///< stage number for Internal (e.g. 5 = drum machine), 0 otherwise

  static Origin fromInput(MidiSource source) { return {source == MidiSource::Usb ? UsbIn : DinIn, 0}; }
  static Origin internal(uint8_t owner) { return {Internal, owner}; }
  bool operator==(const Origin &other) const { return kind == other.kind && owner == other.owner; }
};

/*! The single send path: the router and every generator send through here, so the
 *  note tracker and counters see exactly what went out. Runs in the MIDI task only. */
class MidiOutputs
{
public:
  struct Counters
  {
    uint32_t messages[2] = {0, 0}; ///< per MidiOutput
    uint32_t bytes[2] = {0, 0};
  };

  void begin(DinMidi &din, UsbMidi &usb, NoteTracker &tracker);
  void send(MidiOutput out, const MidiMessage &msg, Origin origin);
  bool available(MidiOutput out) const; ///< false while no USB device is connected
  bool hasSpace(MidiOutput out, uint16_t length) const; ///< a message of `length` bytes fits now
  NoteTracker &tracker() { return *_tracker; }

  const Counters &counters() const { return _counters; }

private:
  DinMidi *_din = nullptr;
  UsbMidi *_usb = nullptr;
  NoteTracker *_tracker = nullptr;
  Counters _counters;
};
