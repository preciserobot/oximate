#pragma once

#include <HardwareSerial.h>
#include "midi_message.h"

/*! Serial (DIN) MIDI port of the M5Stack Unit MIDI.
 *  Turns the incoming byte stream (running status, interleaved realtime bytes)
 *  into complete messages, and writes complete messages out. */
class DinMidi
{
public:
  void begin(HardwareSerial &serial, int8_t rxPin, int8_t txPin, MidiHandler handler);
  void poll();
  void send(const MidiMessage &msg);

private:
  void parse(uint8_t b);
  void emit(const uint8_t *bytes, uint16_t length);

  HardwareSerial *_serial = nullptr;
  MidiHandler _handler = nullptr;

  uint8_t _msg[3] = {0};  // current status (kept for running status) + data
  uint8_t _expected = 0;  // data bytes needed for _msg[0]
  uint8_t _received = 0;  // data bytes received so far

  uint8_t _sysex[kMaxSysExLength];
  uint16_t _sysexLength = 0;
  bool _inSysEx = false;
  bool _sysexOverflow = false;
};
