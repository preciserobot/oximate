#include "din_midi.h"

void DinMidi::begin(HardwareSerial &serial, int8_t rxPin, int8_t txPin, MidiHandler handler)
{
  _serial = &serial;
  _handler = handler;
  // must be set before begin(); DIN is slow (3125 bytes/s) so merged USB traffic needs headroom
  _serial->setRxBufferSize(1024);
  _serial->setTxBufferSize(1024);
  _serial->begin(31250, SERIAL_8N1, rxPin, txPin);
}

void DinMidi::poll()
{
  while (_serial->available()) {
    parse(_serial->read());
  }
}

void DinMidi::send(const MidiMessage &msg)
{
  _serial->write(msg.bytes, msg.length);
}

void DinMidi::emit(const uint8_t *bytes, uint16_t length)
{
  MidiMessage msg = {MidiSource::Din, 0, bytes, length};
  _handler(msg);
}

void DinMidi::parse(uint8_t b)
{
  // Realtime bytes may appear anywhere, even inside other messages
  if (b >= 0xF8) {
    emit(&b, 1);
    return;
  }

  if (b == 0xF0) {
    _inSysEx = true;
    _sysexOverflow = false;
    _sysex[0] = b;
    _sysexLength = 1;
    _msg[0] = 0; // SysEx cancels running status
    return;
  }

  if (b == 0xF7) {
    if (_inSysEx && !_sysexOverflow && _sysexLength < kMaxSysExLength) {
      _sysex[_sysexLength++] = b;
      emit(_sysex, _sysexLength);
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
      emit(&b, 1); // tune request (and undefined F4/F5)
      return;
    }
    _msg[0] = b;
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
  _msg[1 + _received++] = b;
  if (_received == _expected) {
    emit(_msg, 1 + _expected);
    _received = 0;
    if (_msg[0] >= 0xF0) {
      _msg[0] = 0; // system common messages don't support running status
    }
  }
}
