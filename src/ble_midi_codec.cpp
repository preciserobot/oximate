#include "ble_midi_codec.h"
#include <string.h>

// ---------------------------------------------------------------- decoder

void BleMidiDecoder::reset()
{
  _status = 0;
  _have = _need = 0;
  _inSysEx = false;
  _sysExOverflow = false;
  _sysExLength = 0;
}

void BleMidiDecoder::emit(const uint8_t *bytes, uint16_t length, uint32_t nowMicros, MidiHandler handler)
{
  if (!handler) return;
  MidiMessage msg{MidiSource::Ble, 0, bytes, length};
  msg.timeMicros = nowMicros;
  handler(msg);
}

void BleMidiDecoder::decode(const uint8_t *data, size_t length, uint32_t nowMicros, MidiHandler handler)
{
  if (length < 2 || (data[0] & 0xC0) != 0x80) {
    _badPackets++;
    return;
  }
  uint16_t high = (uint16_t)(data[0] & 0x3F) << 7;
  bool timestampSeen = false; // a byte with bit 7 set is a timestamp, the next one a status byte
  for (size_t i = 1; i < length; i++) {
    uint8_t b = data[i];
    if (b & 0x80) {
      if (!timestampSeen) {
        timestampSeen = true;
        _lastTimestamp = high | (b & 0x7F);
        continue;
      }
      timestampSeen = false;
      if (b >= 0xF8) {
        emit(&b, 1, nowMicros, handler); // real-time: doesn't disturb a message or SysEx in progress
        continue;
      }
      if (b == 0xF7) {
        if (_inSysEx) {
          _inSysEx = false;
          if (!_sysExOverflow && _sysExLength < kMaxSysExLength) {
            _sysEx[_sysExLength++] = 0xF7;
            emit(_sysEx, _sysExLength, nowMicros, handler);
          } else {
            _droppedSysEx++;
          }
        }
        continue;
      }
      if (_inSysEx) _droppedSysEx++; // a status byte cuts an unfinished SysEx
      _inSysEx = false;
      if (b == 0xF0) {
        _inSysEx = true;
        _sysExOverflow = false;
        _sysEx[0] = 0xF0;
        _sysExLength = 1;
        _status = 0;
        continue;
      }
      _message[0] = b;
      _have = 1;
      _need = 1 + midiDataLength(b);
      _status = b < 0xF0 ? b : 0; // system common cancels running status
      if (_need == 1) {
        emit(_message, 1, nowMicros, handler); // tune request, undefined
        _have = 0;
      }
      continue;
    }

    // Data byte
    timestampSeen = false;
    if (_inSysEx) {
      if (_sysExLength < kMaxSysExLength - 1) {
        _sysEx[_sysExLength++] = b;
      } else {
        _sysExOverflow = true;
      }
      continue;
    }
    if (_have == 0) {
      if (!_status) continue; // stray data byte
      _message[0] = _status;    // running status
      _have = 1;
      _need = 1 + midiDataLength(_status);
    }
    _message[_have++] = b;
    if (_have >= _need) {
      emit(_message, _have, nowMicros, handler);
      _have = 0;
    }
  }
}

// ---------------------------------------------------------------- encoder

void BleMidiEncoder::setMaxPacket(size_t bytes)
{
  if (bytes < kMinPacket) bytes = kMinPacket;
  if (bytes > kMaxPacket) bytes = kMaxPacket;
  _max = bytes;
}

void BleMidiEncoder::begin(uint16_t timestamp)
{
  _header = 0x80 | ((timestamp >> 7) & 0x3F);
  _length = 0;
  put(_header);
}

void BleMidiEncoder::flush(PacketSink sink, void *context)
{
  if (_length > 1 && sink) sink(context, _packet, _length);
  _length = 0;
}

void BleMidiEncoder::add(const uint8_t *bytes, size_t length, uint16_t millis, PacketSink sink, void *context)
{
  if (!length) return;
  uint16_t timestamp = millis & 0x1FFF;
  uint8_t header = 0x80 | ((timestamp >> 7) & 0x3F);
  uint8_t stamp = 0x80 | (timestamp & 0x7F);
  // A packet has one header: when the high timestamp bits change, start a new packet
  if (_length && _header != header) flush(sink, context);

  if (bytes[0] != 0xF0) {
    if (_length && room() < length + 1) flush(sink, context);
    if (!_length) begin(timestamp);
    if (room() < length + 1) return; // can't happen for normal messages (at most 3 bytes)
    put(stamp);
    for (size_t i = 0; i < length; i++) put(bytes[i]);
    return;
  }

  // SysEx: timestamp F0, data (continuation packets carry data right after the header), timestamp F7
  size_t end = bytes[length - 1] == 0xF7 ? length - 1 : length;
  if (_length && room() < 2) flush(sink, context);
  if (!_length) begin(timestamp);
  put(stamp);
  put(0xF0);
  for (size_t i = 1; i < end; i++) {
    if (bytes[i] & 0x80) continue; // only data bytes inside a SysEx
    if (!room()) {
      flush(sink, context);
      begin(timestamp);
    }
    put(bytes[i]);
  }
  if (room() < 2) {
    flush(sink, context);
    begin(timestamp);
  }
  put(stamp);
  put(0xF7);
}

// ---------------------------------------------------------------- packet ring

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
