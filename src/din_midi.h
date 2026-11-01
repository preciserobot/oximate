#pragma once

#include <HardwareSerial.h>
#include "midi_message.h"

/*! Serial (DIN) MIDI port of the M5Stack Unit MIDI.
 *  Turns the incoming byte stream (running status, interleaved realtime bytes)
 *  into complete messages, and writes complete messages out.
 *  Output goes through two software queues: realtime bytes (clock, start/stop)
 *  always jump ahead of queued notes and SysEx, so clock is never delayed by more
 *  than the few bytes already in the UART FIFO. Runs in the MIDI task only. */
class DinMidi
{
public:
  struct Counters
  {
    uint32_t txDropped = 0; ///< messages that did not fit the output queue
  };

  void begin(HardwareSerial &serial, int8_t rxPin, int8_t txPin, MidiHandler handler);
  void poll();   ///< read and parse received bytes
  void pumpTx(); ///< top up the UART FIFO from the queues; call at least every millisecond
  void send(const MidiMessage &msg);

  const Counters &counters() const { return _counters; }

private:
  static constexpr size_t kFifoLength = 128;   // ESP32 UART hardware FIFO
  static constexpr size_t kFifoTarget = 4;     // bytes kept in the FIFO: bounds realtime latency to ~1.6 ms
  static constexpr size_t kQueueLength = 2048; // normal messages
  static constexpr size_t kRealtimeLength = 64;

  template <size_t N>
  struct ByteQueue
  {
    uint8_t data[N];
    size_t head = 0, tail = 0; // head: next write, tail: next read
    size_t used() const { return (head + N - tail) % N; }
    size_t space() const { return N - 1 - used(); }
    bool empty() const { return head == tail; }
    void push(uint8_t b) { data[head] = b; head = (head + 1) % N; }
    uint8_t pop() { uint8_t b = data[tail]; tail = (tail + 1) % N; return b; }
  };

  void parse(uint8_t b, uint32_t timeMicros);
  void emit(const uint8_t *bytes, uint16_t length, uint32_t timeMicros);

  HardwareSerial *_serial = nullptr;
  MidiHandler _handler = nullptr;
  Counters _counters;

  uint8_t _msg[3] = {0};  // current status (kept for running status) + data
  uint8_t _expected = 0;  // data bytes needed for _msg[0]
  uint8_t _received = 0;  // data bytes received so far
  uint32_t _msgStart = 0; // arrival time of the message's first byte
  bool _startSet = false;

  uint8_t _sysex[kMaxSysExLength];
  uint16_t _sysexLength = 0;
  uint32_t _sysexStart = 0;
  bool _inSysEx = false;
  bool _sysexOverflow = false;

  ByteQueue<kQueueLength> _queue;
  ByteQueue<kRealtimeLength> _realtime;
};
