#pragma once

#include <stdint.h>
#include <stddef.h>

/*! Where a MIDI message entered OxiMate */
enum class MidiSource : uint8_t
{
  Usb = 0, ///< USB MIDI device on the USB host module
  Din = 1, ///< 5-pin DIN / TRS input of the Unit MIDI
};

/*! A complete MIDI message (never running status, never a SysEx fragment).
 *  `bytes` is only valid for the duration of the handler call. */
struct MidiMessage
{
  MidiSource source;
  uint8_t cable;        ///< USB-MIDI virtual cable (always 0 for DIN)
  const uint8_t *bytes; ///< status byte first; SysEx includes F0 ... F7
  uint16_t length;
  uint32_t timeMicros = 0; ///< when the message arrived at its input (0 for generated messages)

  uint8_t status() const { return bytes[0]; }
  bool isChannel() const { return bytes[0] < 0xF0; }
  bool isRealtime() const { return bytes[0] >= 0xF8; }
  bool isSysEx() const { return bytes[0] == 0xF0; }
  uint8_t channel() const { return (bytes[0] & 0x0F) + 1; }
};

typedef void (*MidiHandler)(const MidiMessage &msg);

/*! Largest SysEx message that is assembled and forwarded (bigger ones are dropped) */
constexpr size_t kMaxSysExLength = 1024;

/*! Number of data bytes following a (non-SysEx) status byte */
inline uint8_t midiDataLength(uint8_t status)
{
  if (status < 0xF0) {
    uint8_t type = status & 0xF0;
    return (type == 0xC0 || type == 0xD0) ? 1 : 2;
  }
  switch (status) {
    case 0xF1: return 1; // MTC quarter frame
    case 0xF2: return 2; // song position
    case 0xF3: return 1; // song select
    default:   return 0; // tune request, realtime, undefined
  }
}
