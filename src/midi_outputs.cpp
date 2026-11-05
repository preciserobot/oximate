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

void MidiOutputs::addListener(Listener listener, void *ctx)
{
  if (_listenerCount < kMaxListeners) {
    _listeners[_listenerCount] = listener;
    _listenerCtx[_listenerCount] = ctx;
    _listenerCount++;
  }
}

void MidiOutputs::setPort(MidiOutput out, RadioPort *port)
{
  if ((int)out >= 2 && (int)out < kOutputs) _ports[(int)out] = port;
}

bool MidiOutputs::available(MidiOutput out) const
{
  switch (out) {
    case MidiOutput::Din: return true;
    case MidiOutput::Usb: return _usb->connected();
    default: {
      RadioPort *port = _ports[(int)out & 3];
      return port && port->connected();
    }
  }
}

bool MidiOutputs::hasSpace(MidiOutput out, uint16_t length) const
{
  switch (out) {
    case MidiOutput::Din: return _din->txSpace() >= length;
    case MidiOutput::Usb: return _usb->txSpace() >= length + 3u;
    default: {
      RadioPort *port = _ports[(int)out & 3];
      return port && port->txSpace() >= length;
    }
  }
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
  } else if (out == MidiOutput::Usb) {
    _usb->send(msg);
  } else {
    _ports[(int)out & 3]->send(msg); // available() checked that it exists
  }
  _tracker->sent(out, msg, origin);
  _counters.messages[(int)out]++;
  _counters.bytes[(int)out] += msg.length;
  for (int i = 0; i < _listenerCount; i++) {
    _listeners[i](out, msg, origin, _listenerCtx[i]);
  }
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
