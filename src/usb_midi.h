#pragma once

#include <atomic>
#include "midi_message.h"

/*! USB MIDI device attached to the M5Stack USB host module (MAX3421E).
 *  Turns USB-MIDI event packets into complete messages and sends messages back.
 *  poll() and send() run in the MIDI task. The MAX3421E shares the SPI bus with the
 *  display, so poll() only touches it when the bus lock is free (try-lock), and
 *  send() queues messages that poll() flushes while it holds the lock. */
class UsbMidi
{
public:
  struct Counters
  {
    uint32_t txDropped = 0; ///< messages that did not fit the send queue
    uint32_t busBusy = 0;   ///< polls skipped because the display held the SPI bus
  };

  bool begin(MidiHandler handler); ///< false if the MAX3421E does not respond
  void poll();                      ///< run the USB stack, flush sends, read pending packets
  void send(const MidiMessage &msg);

  // Safe to call from any task
  bool connected() const { return _connected; }
  uint16_t vendorId() const { return _vid; }
  uint16_t productId() const { return _pid; }

  const Counters &counters() const { return _counters; }

private:
  static constexpr size_t kTxQueueLength = 2048;

  void flushTx();
  void parsePacket(const uint8_t *p, uint32_t timeMicros);
  void emit(uint8_t cable, const uint8_t *bytes, uint16_t length, uint32_t timeMicros);
  void appendSysEx(const uint8_t *bytes, uint8_t length);

  MidiHandler _handler = nullptr;
  Counters _counters;
  std::atomic<bool> _connected{false};
  std::atomic<uint16_t> _vid{0}, _pid{0};

  // One SysEx buffer shared by all cables: devices do not interleave SysEx across cables
  uint8_t _sysex[kMaxSysExLength];
  uint16_t _sysexLength = 0;
  uint32_t _sysexStart = 0;
  uint32_t _packetMicros = 0; // arrival time of the packet being parsed
  bool _sysexOverflow = false;

  // Send queue of records: [cable] [length low] [length high] [bytes...]
  uint8_t _tx[kTxQueueLength];
  size_t _txHead = 0, _txTail = 0;
  uint8_t _txMessage[kMaxSysExLength];
};
