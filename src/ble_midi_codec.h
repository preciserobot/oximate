#pragma once

#include <stdint.h>
#include <stddef.h>
#include "midi_message.h"
#include "packet_ring.h"

/*! BLE MIDI packet format (Apple / MMA "MIDI over Bluetooth Low Energy", stage 17).
 *
 *  A packet is a header byte (bit 7 set, bits 5-0 = timestamp bits 12-7) followed by messages, each
 *  one preceded by a timestamp byte (bit 7 set, bits 6-0 = timestamp bits 6-0). Running status is
 *  allowed (data bytes without a new status byte, with or without a new timestamp byte). SysEx may
 *  span packets: continuation packets carry data right after the header, and the closing F7 has its
 *  own timestamp byte. Real-time bytes (with a timestamp) may appear inside a message or a SysEx.
 *  Pure C++, host-tested in test/test_ble.cpp. */

/*! Turns received BLE MIDI packets into complete MidiMessages (source BLE, cable 0) */
class BleMidiDecoder
{
public:
  /*! Decode one packet; complete messages go to `handler` with `timeMicros` = `nowMicros` (the
   *  BLE timestamps say when the sender produced them, but they are only 13 bits of milliseconds
   *  of the sender's clock: not used for scheduling) */
  void decode(const uint8_t *data, size_t length, uint32_t nowMicros, MidiHandler handler);
  void reset();

  /*! The 13-bit timestamp (milliseconds, sender's clock) of the last message delivered */
  uint16_t lastTimestamp() const { return _lastTimestamp; }
  /*! Packets dropped because the header byte was invalid */
  uint32_t badPackets() const { return _badPackets; }
  /*! SysEx messages dropped because they were too long or interrupted */
  uint32_t droppedSysEx() const { return _droppedSysEx; }

private:
  void emit(const uint8_t *bytes, uint16_t length, uint32_t nowMicros, MidiHandler handler);

  uint8_t _status = 0;   ///< running status (channel messages) or system common in progress
  uint8_t _message[3];   ///< message being assembled
  uint8_t _have = 0;     ///< bytes in _message
  uint8_t _need = 0;     ///< bytes a complete message has
  bool _inSysEx = false;
  bool _sysExOverflow = false;
  uint16_t _sysExLength = 0;
  uint8_t _sysEx[kMaxSysExLength];
  uint16_t _lastTimestamp = 0;
  uint32_t _badPackets = 0;
  uint32_t _droppedSysEx = 0;
};

/*! Packs MIDI messages into BLE MIDI packets of at most maxPacket() bytes (ATT MTU - 3) */
class BleMidiEncoder
{
public:
  static constexpr size_t kMaxPacket = 244; ///< the most one packet can hold (MTU 247)
  static constexpr size_t kMinPacket = 20;  ///< default MTU 23

  typedef void (*PacketSink)(void *context, const uint8_t *packet, size_t length);

  void setMaxPacket(size_t bytes);
  size_t maxPacket() const { return _max; }

  /*! Append a complete message (SysEx F0 ... F7 is split across packets as needed); full packets
   *  go to `sink`. Clock and other real-time bytes are encoded like any other message. */
  void add(const uint8_t *bytes, size_t length, uint16_t millis, PacketSink sink, void *context);
  /*! Send the packet being filled, if any */
  void flush(PacketSink sink, void *context);
  bool pending() const { return _length > 0; }

private:
  void begin(uint16_t timestamp);
  void put(uint8_t byte) { _packet[_length++] = byte; }
  size_t room() const { return _max - _length; }

  uint8_t _packet[kMaxPacket];
  size_t _length = 0;
  size_t _max = kMinPacket;
  uint8_t _header = 0;
};
