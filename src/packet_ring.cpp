#include "packet_ring.h"

bool PacketRing::push(const uint8_t *data, size_t length)
{
  size_t head = _head.load(std::memory_order_relaxed);
  size_t tail = _tail.load(std::memory_order_acquire);
  if (length == 0 || length > 0xFFFF || kBytes - (head - tail) < length + 2) {
    _dropped++;
    return false;
  }
  _buffer[head % kBytes] = length & 0xFF;
  _buffer[(head + 1) % kBytes] = length >> 8;
  for (size_t i = 0; i < length; i++) _buffer[(head + 2 + i) % kBytes] = data[i];
  _head.store(head + 2 + length, std::memory_order_release);
  return true;
}

size_t PacketRing::pop(uint8_t *out, size_t max)
{
  size_t tail = _tail.load(std::memory_order_relaxed);
  size_t head = _head.load(std::memory_order_acquire);
  if (head == tail) return 0;
  size_t length = _buffer[tail % kBytes] | (size_t)_buffer[(tail + 1) % kBytes] << 8;
  size_t copy = length < max ? length : max;
  for (size_t i = 0; i < copy; i++) out[i] = _buffer[(tail + 2 + i) % kBytes];
  _tail.store(tail + 2 + length, std::memory_order_release);
  return copy;
}
