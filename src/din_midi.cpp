#include "din_midi.h"

#include <Arduino.h>

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
  // Whole messages only: a partly queued message would corrupt the stream
  if (_queue.space() < msg.length) {
    _counters.txDropped++;
    return;
  }
  for (uint16_t i = 0; i < msg.length; i++) {
    _queue.push(msg.bytes[i]);
  }
}

void DinMidi::pumpTx()
{
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
    _inSysEx = true;
    _sysexOverflow = false;
    _sysex[0] = b;
    _sysexLength = 1;
    _sysexStart = timeMicros;
    _msg[0] = 0; // SysEx cancels running status
    return;
  }

  if (b == 0xF7) {
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
