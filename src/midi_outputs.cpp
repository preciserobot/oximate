#include "midi_outputs.h"

#include "din_midi.h"
#include "note_tracker.h"
#include "usb_midi.h"

void MidiOutputs::begin(DinMidi &din, UsbMidi &usb, NoteTracker &tracker)
{
  _din = &din;
  _usb = &usb;
  _tracker = &tracker;
}

bool MidiOutputs::available(MidiOutput out) const
{
  return out == MidiOutput::Din || _usb->connected();
}

void MidiOutputs::send(MidiOutput out, const MidiMessage &msg, Origin origin)
{
  if (!available(out)) {
    return; // nothing to track: the message never left
  }
  if (out == MidiOutput::Din) {
    _din->send(msg);
  } else {
    _usb->send(msg);
  }
  _tracker->sent(out, msg, origin);
  _counters.messages[(int)out]++;
  _counters.bytes[(int)out] += msg.length;
}
