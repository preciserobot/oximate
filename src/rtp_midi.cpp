#include "rtp_midi.h"
#include <string.h>

using namespace RtpMidi;

// ---------------------------------------------------------------- decoder

void RtpMidiDecoder::reset()
{
  _status = 0;
  _inSysEx = false;
  _sysExLength = 0;
  _sysExOverflow = false;
}

void RtpMidiDecoder::emit(const uint8_t *bytes, uint16_t length, uint32_t nowMicros, MidiHandler handler)
{
  if (!handler) return;
  MidiMessage msg{MidiSource::Net, 0, bytes, length};
  msg.timeMicros = nowMicros;
  handler(msg);
}

void RtpMidiDecoder::decode(const uint8_t *payload, size_t length, uint32_t nowMicros, MidiHandler handler)
{
  if (length < 1) return;
  uint8_t flags = payload[0];
  size_t pos = 1;
  size_t listLength = flags & 0x0F;
  if (flags & 0x80) { // B: 12-bit length
    if (length < 2) return;
    listLength = listLength << 8 | payload[1];
    pos = 2;
  }
  size_t end = pos + listLength;
  if (end > length) end = length; // truncated: decode what is there
  bool deltaFirst = flags & 0x20; // Z
  _status = 0;                    // running status never crosses packets (P is not supported)

  bool first = true;
  while (pos < end) {
    if (!first || deltaFirst) {
      for (int i = 0; i < 4 && pos < end; i++) { // delta time: 1-4 bytes, bit 7 = more follow
        if (!(payload[pos++] & 0x80)) break;
      }
      if (pos >= end) break;
    }
    first = false;

    uint8_t b = payload[pos];
    if (b == 0xF0 || b == 0xF7) {
      // SysEx (segment): F0 ... F7 whole, F0 ... F0 first, F7 ... F0 middle, F7 ... F7 last, ... F4 cancel
      bool continuation = b == 0xF7;
      if (!continuation) {
        if (_inSysEx) _droppedSysEx++; // the previous one never ended
        _inSysEx = true;
        _sysExOverflow = false;
        _sysEx[0] = 0xF0;
        _sysExLength = 1;
      }
      bool known = _inSysEx;
      pos++;
      uint8_t terminator = 0;
      while (pos < end) {
        uint8_t d = payload[pos++];
        if (d >= 0xF8) {
          emit(&d, 1, nowMicros, handler); // real-time inside a SysEx
          continue;
        }
        if (d & 0x80) {
          terminator = d;
          break;
        }
        if (!known) continue;
        if (_sysExLength < kMaxSysExLength - 1) {
          _sysEx[_sysExLength++] = d;
        } else {
          _sysExOverflow = true;
        }
      }
      if (!known) continue; // a continuation of a SysEx we never saw start
      if (terminator == 0xF7) {
        _inSysEx = false;
        if (_sysExOverflow) {
          _droppedSysEx++;
        } else {
          _sysEx[_sysExLength++] = 0xF7;
          emit(_sysEx, _sysExLength, nowMicros, handler);
        }
      } else if (terminator == 0xF0) {
        // more in the next command or packet
      } else {
        if (terminator == 0xF4 || terminator == 0) _droppedSysEx++; // cancelled or cut off
        _inSysEx = false;
      }
      _status = 0;
      continue;
    }

    uint8_t message[3];
    if (b & 0x80) {
      pos++;
      if (b >= 0xF8) {
        emit(&b, 1, nowMicros, handler); // real-time: running status unchanged
        continue;
      }
      message[0] = b;
      _status = b < 0xF0 ? b : 0; // system common cancels running status
    } else {
      if (!_status) { // data without a status: skip this command
        pos++;
        continue;
      }
      message[0] = _status;
    }
    uint8_t need = midiDataLength(message[0]);
    if (pos + need > end) break;
    for (uint8_t i = 0; i < need; i++) {
      if (payload[pos] & 0x80) break; // malformed: a status where data belongs
      message[1 + i] = payload[pos++];
      if (i + 1 == need) emit(message, 1 + need, nowMicros, handler);
    }
    if (need == 0) emit(message, 1, nowMicros, handler);
  }
}

// ---------------------------------------------------------------- encoder

void RtpMidiEncoder::start(uint32_t timestamp)
{
  _length = kHeader;
  _commands = 0;
  _timestamp = timestamp;
}

void RtpMidiEncoder::flush(PacketSink sink, void *context)
{
  if (_commands > 0) {
    _packet[0] = 0x80; // version 2, no padding, extension or CSRC
    _packet[1] = kPayloadType;
    put16(_packet + 2, _sequence++);
    put32(_packet + 4, _timestamp);
    put32(_packet + 8, _ssrc);
    size_t list = _length - kHeader;
    _packet[12] = 0x80 | ((list >> 8) & 0x0F); // B = 1, J = 0, Z = 0, P = 0
    _packet[13] = list & 0xFF;
    if (sink) sink(context, _packet, _length);
  }
  _length = 0;
  _commands = 0;
}

void RtpMidiEncoder::add(const uint8_t *bytes, size_t length, uint32_t timestamp, PacketSink sink, void *context)
{
  if (!length) return;
  if (!_length) start(timestamp);

  if (bytes[0] != 0xF0) {
    size_t delta = _commands ? 1 : 0;
    if (room() < delta + length) {
      flush(sink, context);
      start(timestamp);
      delta = 0;
    }
    if (delta) _packet[_length++] = 0x00;
    memcpy(_packet + _length, bytes, length);
    _length += length;
    _commands++;
    return;
  }

  // SysEx: whole if it fits, else segments (F0 ... F0, F7 ... F0, F7 ... F7) over several packets
  const uint8_t *data = bytes + 1;
  size_t count = bytes[length - 1] == 0xF7 ? length - 2 : length - 1;
  size_t done = 0;
  bool firstSegment = true;
  for (;;) {
    size_t delta = _commands ? 1 : 0;
    size_t remaining = count - done;
    if (room() >= delta + remaining + 2) { // lead byte + data + F7
      if (delta) _packet[_length++] = 0x00;
      _packet[_length++] = firstSegment ? 0xF0 : 0xF7;
      memcpy(_packet + _length, data + done, remaining);
      _length += remaining;
      _packet[_length++] = 0xF7;
      _commands++;
      return;
    }
    if (_commands && room() < delta + 64) { // not worth starting a segment here
      flush(sink, context);
      start(timestamp);
      continue;
    }
    size_t chunk = room() - delta - 2; // lead byte + F0
    if (delta) _packet[_length++] = 0x00;
    _packet[_length++] = firstSegment ? 0xF0 : 0xF7;
    memcpy(_packet + _length, data + done, chunk);
    _length += chunk;
    _packet[_length++] = 0xF0; // continues in the next packet
    _commands++;
    done += chunk;
    firstSegment = false;
    flush(sink, context);
    start(timestamp);
  }
}

// ---------------------------------------------------------------- session

static bool isCommand(const uint8_t *data, size_t length, const char *command)
{
  return length >= 4 && data[0] == 0xFF && data[1] == 0xFF && data[2] == (uint8_t)command[0] &&
         data[3] == (uint8_t)command[1];
}

void AppleMidiSession::begin(const char *name, uint32_t ssrc, Sender sender, void *context)
{
  strncpy(_name, name, sizeof(_name) - 1);
  _name[sizeof(_name) - 1] = 0;
  _ssrc = ssrc;
  _sender = sender;
  _context = context;
}

void AppleMidiSession::reply(bool dataPort, Endpoint to, const char *command, uint32_t token)
{
  uint8_t packet[16 + sizeof(_name)];
  packet[0] = packet[1] = 0xFF;
  packet[2] = command[0];
  packet[3] = command[1];
  put32(packet + 4, kProtocolVersion);
  put32(packet + 8, token);
  put32(packet + 12, _ssrc);
  size_t length = 16;
  if (command[0] == 'O') { // OK carries our name
    size_t n = strlen(_name) + 1;
    memcpy(packet + 16, _name, n);
    length += n;
  }
  if (_sender) _sender(_context, dataPort, to, packet, length);
}

void AppleMidiSession::end()
{
  _state = State::Idle;
  _peerName[0] = 0;
  _peerSsrc = 0;
  _haveSequence = false;
  _feedbackDue = false;
  _latency = 0;
  _decoder.reset();
}

void AppleMidiSession::onControl(const uint8_t *data, size_t length, Endpoint from, uint64_t, uint32_t nowMillis)
{
  if (isCommand(data, length, "IN") && length >= 16) {
    uint32_t token = get32(data + 8);
    uint32_t ssrc = get32(data + 12);
    if (_state != State::Idle && ssrc != _peerSsrc) {
      reply(false, from, "NO", token); // one session at a time
      return;
    }
    _state = State::ControlOk;
    _control = from;
    _peerSsrc = ssrc;
    size_t n = 0;
    for (size_t i = 16; i < length && data[i] && n < sizeof(_peerName) - 1; i++) _peerName[n++] = data[i];
    _peerName[n] = 0;
    _lastSync = nowMillis;
    reply(false, from, "OK", token);
  } else if (isCommand(data, length, "BY") && length >= 16) {
    if (get32(data + 12) == _peerSsrc && _state != State::Idle) end();
  }
}

void AppleMidiSession::onData(const uint8_t *data, size_t length, Endpoint from, uint64_t now100us,
                              uint32_t nowMillis, uint32_t nowMicros, MidiHandler handler)
{
  if (length >= 4 && data[0] == 0xFF && data[1] == 0xFF) {
    if (isCommand(data, length, "IN") && length >= 16) {
      uint32_t token = get32(data + 8);
      if (_state == State::Idle || get32(data + 12) != _peerSsrc) {
        reply(true, from, "NO", token);
        return;
      }
      _data = from;
      _state = State::Connected;
      _haveSequence = false;
      _decoder.reset();
      _lastSync = nowMillis;
      reply(true, from, "OK", token);
    } else if (isCommand(data, length, "CK") && length >= 36) {
      if (get32(data + 4) != _peerSsrc || _state != State::Connected) return;
      uint8_t count = data[8];
      _lastSync = nowMillis;
      if (count == 0) { // the peer starts a sync: answer with our time
        uint8_t packet[36];
        memcpy(packet, data, 36);
        put32(packet + 4, _ssrc);
        packet[8] = 1;
        put64(packet + 20, now100us);
        if (_sender) _sender(_context, true, from, packet, 36);
      } else if (count == 2) {
        uint64_t t1 = get64(data + 12), t3 = get64(data + 28);
        if (t3 > t1) _latency = (uint32_t)((t3 - t1) / 2);
      }
    } else if (isCommand(data, length, "BY") && length >= 16) {
      if (get32(data + 12) == _peerSsrc && _state != State::Idle) end();
    }
    return;
  }

  // RTP-MIDI
  if (_state != State::Connected || length < 13) return;
  if ((data[0] & 0xC0) != 0x80 || (data[1] & 0x7F) != kPayloadType || get32(data + 8) != _peerSsrc) return;
  uint16_t sequence = get16(data + 2);
  if (_haveSequence) {
    uint16_t gap = sequence - _lastSequence;
    if (gap == 0 || gap > 0x8000) return; // duplicate or late
    _lost += gap - 1;
  }
  _haveSequence = true;
  _lastSequence = sequence;
  _feedbackDue = true;
  _received++;
  size_t pos = 12 + (data[0] & 0x0F) * 4; // CSRCs
  if ((data[0] & 0x10) && pos + 4 <= length) pos += 4 + get16(data + pos + 2) * 4; // extension
  if (pos >= length) return;
  _decoder.decode(data + pos, length - pos, nowMicros, handler);
}

void AppleMidiSession::poll(uint32_t nowMillis)
{
  if (_state == State::Idle) return;
  if (nowMillis - _lastSync > kTimeoutMillis) {
    end(); // the Mac slept or left without BY
    return;
  }
  if (_state == State::Connected && _feedbackDue && nowMillis - _lastFeedback >= kFeedbackMillis) {
    _lastFeedback = nowMillis;
    _feedbackDue = false;
    uint8_t packet[12] = {0xFF, 0xFF, 'R', 'S'};
    put32(packet + 4, _ssrc);
    put16(packet + 8, _lastSequence);
    packet[10] = packet[11] = 0;
    if (_sender) _sender(_context, false, _control, packet, sizeof(packet));
  }
}

void AppleMidiSession::sendData(const uint8_t *packet, size_t length)
{
  if (_state == State::Connected && _sender) _sender(_context, true, _data, packet, length);
}
