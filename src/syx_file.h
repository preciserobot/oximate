#pragma once

#include <stddef.h>
#include <stdint.h>

/*! .syx files and SysEx bytes for the SysEx librarian (stage 14). Plain C++ (host-tested). */
namespace Syx
{
struct Message
{
  uint32_t offset; ///< of its F0 in the file
  uint32_t length; ///< F0 ... F7 inclusive
};

struct Stats
{
  uint32_t messages = 0; ///< complete messages found (may exceed what fitted in the list)
  uint32_t bytes = 0;    ///< bytes in complete messages
  uint32_t stray = 0;    ///< bytes outside any message (skipped)
  bool truncated = false; ///< the file ends inside a message (that message is dropped)
};

/*! Split a .syx file into its F0 ... F7 messages. A status byte other than F7 inside a message
 *  ends it without being complete (it is counted as stray). Returns the number stored in `out`. */
int split(const uint8_t *data, size_t length, Message *out, int max, Stats &stats);

/*! Manufacturer of a message, from its ID (1 byte, or 00 + 2 bytes): "Moog", "Elektron", ... */
const char *manufacturer(const uint8_t *message, size_t length);
/*! The same in lower case without spaces, for file names: "moog", "elektron", "sysex" */
const char *slug(const uint8_t *message, size_t length);

/*! Universal SysEx (7E / 7F) addressed to device 7F: every device on the output reacts */
bool isBroadcast(const uint8_t *message, size_t length);

/*! "<slug>-YYYYMMDD-HHMMSS.syx" when the clock is set (year >= 2024), else "<slug>-NNNN.syx" */
void fileName(char *out, size_t size, const char *slugName, int year, int month, int day, int hour,
              int minute, int second, uint16_t counter);

/*! USB-MIDI SysEx packets: 3 bytes per packet with CIN 4, the last one with CIN 5/6/7 for 1/2/3
 *  bytes ending with F7. Feed bytes; a complete 4-byte packet comes out when ready. */
class UsbPacker
{
public:
  /*! Returns true when `packet` holds a packet to send */
  bool push(uint8_t byte, uint8_t cable, uint8_t packet[4]);
  void reset() { _count = 0; }

private:
  uint8_t _bytes[3] = {0, 0, 0};
  uint8_t _count = 0;
};
} // namespace Syx
