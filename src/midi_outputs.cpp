#include "midi_outputs.h"

#include <Arduino.h>
#include <string.h>

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

bool MidiOutputs::hasSpace(MidiOutput out, uint16_t length) const
{
  return out == MidiOutput::Din ? _din->txSpace() >= length : _usb->txSpace() >= length + 3u;
}

void MidiOutputs::send(MidiOutput out, const MidiMessage &msg, Origin origin)
{
  if (!available(out)) {
    return; // nothing to track: the message never left
  }
  if (out == MidiOutput::Din) {
    _din->send(msg);
    if (msg.isChannel() && msg.length <= 3) { // remembered for echo detection
      Sent &sent = _sent[_sentNext];
      _sentNext = (_sentNext + 1) % kSentHistory;
      sent.timeMicros = micros();
      sent.length = msg.length;
      memcpy(sent.bytes, msg.bytes, msg.length);
    }
  } else {
    _usb->send(msg);
  }
  _tracker->sent(out, msg, origin);
  _counters.messages[(int)out]++;
  _counters.bytes[(int)out] += msg.length;
}

bool MidiOutputs::isEcho(const MidiMessage &msg, uint32_t nowMicros)
{
  if (!msg.isChannel() || msg.length > 3) {
    return false;
  }
  for (Sent &sent : _sent) {
    if (sent.length == msg.length && nowMicros - sent.timeMicros < kEchoWindowMicros &&
        memcmp(sent.bytes, msg.bytes, msg.length) == 0) {
      sent.length = 0; // one echo per sent message
      _counters.echoesBlocked++;
      return true;
    }
  }
  return false;
}
