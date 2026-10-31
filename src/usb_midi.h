#pragma once

#include "midi_message.h"

/*! USB MIDI device attached to the M5Stack USB host module (MAX3421E).
 *  Turns USB-MIDI event packets into complete messages and sends messages back. */
class UsbMidi
{
public:
  bool begin(MidiHandler handler); ///< false if the MAX3421E does not respond
  void poll();                      ///< run the USB stack and read pending packets
  void send(const MidiMessage &msg);

  bool connected();
  uint16_t vendorId();
  uint16_t productId();

private:
  void parsePacket(const uint8_t *p);
  void emit(uint8_t cable, const uint8_t *bytes, uint16_t length);
  void appendSysEx(const uint8_t *bytes, uint8_t length);

  MidiHandler _handler = nullptr;

  // One SysEx buffer shared by all cables: devices do not interleave SysEx across cables
  uint8_t _sysex[kMaxSysExLength];
  uint16_t _sysexLength = 0;
  bool _sysexOverflow = false;
};
