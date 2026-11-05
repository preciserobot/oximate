#pragma once

#include <stddef.h>
#include <stdint.h>
#include "midi_message.h"

/*! RTP-MIDI (RFC 6295) with Apple's session protocol ("AppleMIDI"), the network MIDI that macOS
 *  and iOS have built in (stage 18). Own minimal implementation, pure C++ and host-tested in
 *  test/test_rtpmidi.cpp: OxiMate only answers invitations (the Mac or iPad starts the session),
 *  keeps one session, sends without a recovery journal (J = 0; hosts accept that, they just can't
 *  recover a lost packet) and ignores the journal of what it receives. */

namespace RtpMidi
{
constexpr uint16_t kControlPort = 5004; ///< the data port is kControlPort + 1
constexpr uint8_t kPayloadType = 0x61;
constexpr uint32_t kProtocolVersion = 2;

inline uint16_t get16(const uint8_t *p) { return (uint16_t)(p[0] << 8 | p[1]); }
inline uint32_t get32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | p[2] << 8 | p[3]; }
inline uint64_t get64(const uint8_t *p) { return (uint64_t)get32(p) << 32 | get32(p + 4); }
inline void put16(uint8_t *p, uint16_t v) { p[0] = v >> 8; p[1] = v; }
inline void put32(uint8_t *p, uint32_t v) { p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v; }
inline void put64(uint8_t *p, uint64_t v) { put32(p, v >> 32); put32(p + 4, (uint32_t)v); }
} // namespace RtpMidi

/*! The MIDI command section of received RTP-MIDI packets to complete MidiMessages (source NET).
 *  Handles delta times, running status, real-time bytes and SysEx segmented across packets. */
class RtpMidiDecoder
{
public:
  /*! `payload`: the RTP payload (after the 12-byte RTP header) */
  void decode(const uint8_t *payload, size_t length, uint32_t nowMicros, MidiHandler handler);
  void reset();
  uint32_t droppedSysEx() const { return _droppedSysEx; }

private:
  void emit(const uint8_t *bytes, uint16_t length, uint32_t nowMicros, MidiHandler handler);

  uint8_t _status = 0; ///< running status within one packet (RFC 6295 resets it per packet)
  bool _inSysEx = false; ///< a segmented SysEx continues in the next packet
  uint16_t _sysExLength = 0;
  bool _sysExOverflow = false;
  uint8_t _sysEx[kMaxSysExLength];
  uint32_t _droppedSysEx = 0;
};

/*! Packs messages into RTP-MIDI packets (no journal, delta time 0, full status bytes) */
class RtpMidiEncoder
{
public:
  static constexpr size_t kMaxPacket = 512; ///< RTP header + command section; bigger SysEx is segmented
  typedef void (*PacketSink)(void *context, const uint8_t *packet, size_t length);

  void begin(uint32_t ssrc) { _ssrc = ssrc; }
  /*! `timestamp`: RTP time (100 us units) of the packet this message starts */
  void add(const uint8_t *bytes, size_t length, uint32_t timestamp, PacketSink sink, void *context);
  void flush(PacketSink sink, void *context);
  bool pending() const { return _commands > 0; }
  uint16_t sequence() const { return _sequence; }

private:
  static constexpr size_t kHeader = 12 + 2; ///< RTP header + long command section header
  void start(uint32_t timestamp);
  size_t room() const { return kMaxPacket - _length; }

  uint8_t _packet[kMaxPacket];
  size_t _length = 0;
  int _commands = 0;
  uint32_t _ssrc = 0;
  uint16_t _sequence = 0;
  uint32_t _timestamp = 0;
};

/*! One AppleMIDI session, OxiMate answering. Feed it every UDP packet of the two ports; it sends
 *  its replies through the sink, and MIDI it received to the handler. Network task only. */
class AppleMidiSession
{
public:
  static constexpr uint32_t kTimeoutMillis = 60000; ///< no clock sync for this long: the peer is gone
  static constexpr uint32_t kFeedbackMillis = 1000; ///< receiver feedback (lets the peer trim its journal)

  struct Endpoint
  {
    uint32_t ip = 0;
    uint16_t port = 0;
  };
  /*! Send `data` from the control port (`dataPort` false) or the data port, to `to` */
  typedef void (*Sender)(void *context, bool dataPort, Endpoint to, const uint8_t *data, size_t length);

  void begin(const char *name, uint32_t ssrc, Sender sender, void *context);

  /*! A packet arrived on the control port / the data port. `now100us`: our RTP clock. */
  void onControl(const uint8_t *data, size_t length, Endpoint from, uint64_t now100us, uint32_t nowMillis);
  void onData(const uint8_t *data, size_t length, Endpoint from, uint64_t now100us, uint32_t nowMillis,
              uint32_t nowMicros, MidiHandler handler);
  /*! Housekeeping: timeout, receiver feedback */
  void poll(uint32_t nowMillis);

  /*! Send a packet of the encoder (from the data port, to the peer) */
  void sendData(const uint8_t *packet, size_t length);

  bool connected() const { return _state == State::Connected; }
  const char *peerName() const { return _peerName; }
  /*! Round trip / 2 of the last clock sync, in 100 us units; 0 if none yet */
  uint32_t latency100us() const { return _latency; }
  uint32_t ssrc() const { return _ssrc; }
  uint32_t lostPackets() const { return _lost; }
  uint32_t receivedPackets() const { return _received; }

private:
  enum class State : uint8_t { Idle, ControlOk, Connected };
  void reply(bool dataPort, Endpoint to, const char *command, uint32_t token);
  void end();

  State _state = State::Idle;
  char _name[24] = "OxiMate";
  uint32_t _ssrc = 0;
  Sender _sender = nullptr;
  void *_context = nullptr;

  Endpoint _control, _data; ///< the peer's ports
  uint32_t _peerSsrc = 0;
  char _peerName[24] = "";
  uint32_t _lastSync = 0;
  uint32_t _lastFeedback = 0;
  uint32_t _latency = 0;

  bool _haveSequence = false;
  uint16_t _lastSequence = 0;
  bool _feedbackDue = false;
  uint32_t _lost = 0;
  uint32_t _received = 0;
  RtpMidiDecoder _decoder;
};
