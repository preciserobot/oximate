#pragma once

#include <atomic>
#include <stddef.h>
#include <stdint.h>

/*! Single-producer single-consumer queue of variable-length packets or messages (a radio stack's
 *  task writes, the MIDI task reads, or the other way round); one that doesn't fit is dropped and
 *  counted. Used by BLE MIDI (stage 17) and network MIDI (stage 18). */
class PacketRing
{
public:
  static constexpr size_t kBytes = 2048;

  bool push(const uint8_t *data, size_t length);
  /*! Copy the oldest packet into `out` (cut to `max` bytes); its length, 0 if empty */
  size_t pop(uint8_t *out, size_t max);
  size_t used() const { return _head.load() - _tail.load(); }
  size_t freeBytes() const { return kBytes - used(); }
  uint32_t dropped() const { return _dropped; }

private:
  uint8_t _buffer[kBytes];
  std::atomic<size_t> _head{0}; ///< written by the producer (free-running)
  std::atomic<size_t> _tail{0}; ///< written by the consumer (free-running)
  uint32_t _dropped = 0;
};
