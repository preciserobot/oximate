#pragma once
// Host stand-in for the ESP32 HardwareSerial: input bytes are fed by tests,
// output bytes land in `out`, and `fifo` simulates the 128-byte UART TX FIFO
#include <stddef.h>
#include <stdint.h>
#include <vector>
#define SERIAL_8N1 0
struct HardwareSerial
{
  std::vector<uint8_t> in, out;
  size_t pos = 0;
  size_t fifo = 0; // bytes currently in the TX FIFO
  size_t setRxBufferSize(size_t n) { return n; }
  size_t setTxBufferSize(size_t n) { return n; }
  void begin(long, int, int, int) {}
  int available() { return pos < in.size(); }
  int read() { return in[pos++]; }
  int availableForWrite() { return 128 - fifo; }
  size_t write(uint8_t b) { out.push_back(b); fifo++; return 1; }
  size_t write(const uint8_t *b, size_t n) { for (size_t i = 0; i < n; i++) write(b[i]); return n; }
  void drain(size_t n) { fifo = fifo > n ? fifo - n : 0; } // bytes leave the wire
};
