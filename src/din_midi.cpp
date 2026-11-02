#include "din_midi.h"

#include <Arduino.h>

#include "input_observers.h"

void DinMidi::begin(HardwareSerial &serial, int8_t rxPin, int8_t txPin, MidiHandler handler)
{
  _serial = &serial;
  _handler = handler;
  _rxPin = rxPin;
  _txPin = txPin;
  // Must be set before begin(). No TX ring buffer: writes go straight to the FIFO, so
  // availableForWrite() reports FIFO space and pumpTx() controls what goes out when.
  _serial->setRxBufferSize(1024);
  _serial->setTxBufferSize(0);
  _serial->begin(31250, SERIAL_8N1, rxPin, txPin);
}

void DinMidi::poll()
{
  while (_serial->available()) {
    _counters.rxBytes++;
    parse(_serial->read(), micros());
  }
}

void DinMidi::send(const MidiMessage &msg)
{
  if (msg.isRealtime()) {
    if (_realtime.space() >= 1) {
      _realtime.push(msg.bytes[0]);
    } else {
      _counters.txDropped++;
    }
    return;
  }
  // While a streamed SysEx owns the stream (and until what waited behind it is out), other
  // messages queue up behind it, whole, in their order
  if (_rawOpen || !_held.empty()) {
    if (_held.space() < msg.length) {
      _counters.heldDropped++;
      return;
    }
    for (uint16_t i = 0; i < msg.length; i++) _held.push(msg.bytes[i]);
    return;
  }
  // Whole messages only: a partly queued message would corrupt the stream
  if (_queue.space() < msg.length) {
    _counters.txDropped++;
    return;
  }
  for (uint16_t i = 0; i < msg.length; i++) {
    _queue.push(msg.bytes[i]);
  }
}

void DinMidi::sendRaw(const uint8_t *bytes, size_t length)
{
  for (size_t i = 0; i < length && _queue.space() > 0; i++) {
    if (bytes[i] == 0xF0) _rawOpen = true;
    _queue.push(bytes[i]);
    if (bytes[i] == 0xF7) _rawOpen = false;
  }
}

/*! Move messages that waited for a streamed SysEx into the queue, behind its F7 */
void DinMidi::releaseHeld()
{
  while (!_rawOpen && !_held.empty() && _queue.space() > 0) {
    _queue.push(_held.pop());
  }
}

void DinMidi::pumpTx()
{
  releaseHeld();
  size_t inFifo = kFifoLength - _serial->availableForWrite();
  // Realtime bytes may legally interrupt any message, including SysEx
  while (inFifo < kFifoTarget && (!_realtime.empty() || !_queue.empty())) {
    _serial->write(_realtime.empty() ? _queue.pop() : _realtime.pop());
    _counters.txBytes++;
    inFifo++;
  }
}

void DinMidi::emit(const uint8_t *bytes, uint16_t length, uint32_t timeMicros)
{
  MidiMessage msg = {MidiSource::Din, 0, bytes, length, timeMicros};
  _handler(msg);
}

void DinMidi::parse(uint8_t b, uint32_t timeMicros)
{
  // Realtime bytes may appear anywhere, even inside other messages
  if (b >= 0xF8) {
    emit(&b, 1, timeMicros);
    return;
  }

  if (b == 0xF0) {
    InputHooks::sysex(MidiSource::Din, &b, 1);
    _inSysEx = true;
    _sysexOverflow = false;
    _sysex[0] = b;
    _sysexLength = 1;
    _sysexStart = timeMicros;
    _msg[0] = 0; // SysEx cancels running status
    return;
  }

  if (b == 0xF7) {
    if (_inSysEx) InputHooks::sysex(MidiSource::Din, &b, 1);
    if (_inSysEx && !_sysexOverflow && _sysexLength < kMaxSysExLength) {
      _sysex[_sysexLength++] = b;
      emit(_sysex, _sysexLength, _sysexStart);
    }
    _inSysEx = false;
    return;
  }

  if (b & 0x80) {
    // Any other status byte terminates an unfinished SysEx (which is dropped)
    _inSysEx = false;
    _expected = midiDataLength(b);
    _received = 0;
    if (_expected == 0) {
      _msg[0] = 0;
      emit(&b, 1, timeMicros); // tune request (and undefined F4/F5)
      return;
    }
    _msg[0] = b;
    _msgStart = timeMicros;
    _startSet = true;
    return;
  }

  // Data byte
  if (_inSysEx) {
    InputHooks::sysex(MidiSource::Din, &b, 1); // dumps of any size, past the assembly limit
    if (_sysexLength < kMaxSysExLength - 1) {
      _sysex[_sysexLength++] = b;
    } else {
      _sysexOverflow = true;
    }
    return;
  }
  if (_msg[0] == 0) {
    return; // stray data byte without status
  }
  if (!_startSet) {
    _msgStart = timeMicros; // running status: the message starts with its first data byte
    _startSet = true;
  }
  _msg[1 + _received++] = b;
  if (_received == _expected) {
    emit(_msg, 1 + _expected, _msgStart);
    _received = 0;
    _startSet = false;
    if (_msg[0] >= 0xF0) {
      _msg[0] = 0; // system common messages don't support running status
    }
  }
}
