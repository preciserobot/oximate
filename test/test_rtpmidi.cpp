// Tests for the RTP-MIDI codec and the AppleMIDI session (stage 18)
#include <Arduino.h>
#include "test_support.h"
#include "rtp_midi.h"

using namespace RtpMidi;

static std::vector<MidiSource> gNetSources;
static void recordNet(const MidiMessage &m)
{
  gNetSources.push_back(m.source);
  record(m);
}

/*! A command section with a short header (B = 0) */
static std::vector<uint8_t> section(std::vector<uint8_t> list, uint8_t flags = 0)
{
  std::vector<uint8_t> s = {(uint8_t)(flags | list.size())};
  s.insert(s.end(), list.begin(), list.end());
  return s;
}

static void decode(RtpMidiDecoder &d, const std::vector<uint8_t> &payload)
{
  d.decode(payload.data(), payload.size(), 77, recordNet);
}

TEST(testRtpDecode)
{
  RtpMidiDecoder d;
  gReceived.clear();
  gReceivedTimes.clear();
  gNetSources.clear();
  // one note: no delta time before the first command (Z = 0)
  decode(d, section({0x90, 60, 100}));
  CHECK(gReceived.size() == 1 && gReceived[0] == "90 3C 64 " && gReceivedTimes[0] == 77);
  CHECK(gNetSources[0] == MidiSource::Net);

  // delta times (one and two bytes), running status, program change
  gReceived.clear();
  decode(d, section({0x90, 60, 100, 0x00, 62, 90, 0x81, 0x10, 0xC3, 5, 0x05, 0xB0, 7, 64}));
  CHECK(gReceived.size() == 4);
  CHECK(gReceived[1] == "90 3E 5A " && gReceived[2] == "C3 05 " && gReceived[3] == "B0 07 40 ");

  // Z = 1: a delta time before the first command too
  gReceived.clear();
  decode(d, section({0x0A, 0x80, 60, 0}, 0x20));
  CHECK(gReceived.size() == 1 && gReceived[0] == "80 3C 00 ");

  // long header (B = 1), and a journal after the list (J = 1) is ignored
  gReceived.clear();
  std::vector<uint8_t> p = {0xC0, 3, 0xB1, 1, 2, 0x99, 0x99, 0x99};
  decode(d, p);
  CHECK(gReceived.size() == 1 && gReceived[0] == "B1 01 02 ");

  // running status doesn't cross packets; real-time leaves it alone
  gReceived.clear();
  decode(d, section({61, 1}));
  CHECK(gReceived.empty());
  decode(d, section({0x92, 1, 2, 0x00, 0xF8, 0x00, 3, 4}));
  CHECK(gReceived.size() == 3 && gReceived[1] == "F8 " && gReceived[2] == "92 03 04 ");

  // truncated list: what is complete gets through
  gReceived.clear();
  std::vector<uint8_t> cut = {0x06, 0x90, 1, 2, 0x00, 0x90};
  decode(d, cut);
  CHECK(gReceived.size() == 1);
}

TEST(testRtpDecodeSysEx)
{
  RtpMidiDecoder d;
  gReceived.clear();
  decode(d, section({0xF0, 0x7E, 0x7F, 0x06, 0x01, 0xF7, 0x00, 0x90, 1, 2}));
  CHECK(gReceived.size() == 2 && gReceived[0] == "F0 7E 7F 06 01 F7 " && gReceived[1] == "90 01 02 ");

  // segmented over three packets: F0 .. F0, F7 .. F0, F7 .. F7
  gReceived.clear();
  decode(d, section({0xF0, 1, 2, 0xF0}));
  decode(d, section({0xF7, 3, 0xF8, 4, 0xF0}));
  CHECK(gReceived.size() == 1 && gReceived[0] == "F8 ");
  decode(d, section({0xF7, 5, 0xF7}));
  CHECK(gReceived.size() == 2 && gReceived[1] == "F0 01 02 03 04 05 F7 ");

  // cancelled (F4) and orphan continuation: nothing
  gReceived.clear();
  decode(d, section({0xF0, 1, 0xF0}));
  decode(d, section({0xF7, 2, 0xF4}));
  decode(d, section({0xF7, 3, 0xF7}));
  CHECK(gReceived.empty() && d.droppedSysEx() == 1);
}

static std::vector<std::vector<uint8_t>> gRtpPackets;
static void sinkRtp(void *, const uint8_t *packet, size_t length)
{
  gRtpPackets.push_back(std::vector<uint8_t>(packet, packet + length));
}

TEST(testRtpEncodeRoundTrip)
{
  RtpMidiEncoder e;
  e.begin(0x11223344);
  gRtpPackets.clear();
  uint8_t note[] = {0x90, 60, 100}, pc[] = {0xC1, 9};
  e.add(note, 3, 1000, sinkRtp, nullptr);
  e.add(pc, 2, 1001, sinkRtp, nullptr);
  CHECK(gRtpPackets.empty() && e.pending());
  e.flush(sinkRtp, nullptr);
  CHECK(gRtpPackets.size() == 1);
  std::vector<uint8_t> &p = gRtpPackets[0];
  CHECK(p.size() == 14 + 6);
  CHECK(p[0] == 0x80 && p[1] == kPayloadType && get16(&p[2]) == 0 && get32(&p[4]) == 1000 && get32(&p[8]) == 0x11223344);
  CHECK(p[12] == 0x80 && p[13] == 6);
  CHECK(std::vector<uint8_t>(p.begin() + 14, p.end()) == std::vector<uint8_t>({0x90, 60, 100, 0x00, 0xC1, 9}));
  e.flush(sinkRtp, nullptr);
  CHECK(gRtpPackets.size() == 1 && e.sequence() == 1);

  // big SysEx: segmented, every packet within the limit, decodes back to the same message
  std::vector<uint8_t> sysex = {0xF0};
  for (int i = 0; i < 1000; i++) sysex.push_back(i & 0x7F);
  sysex.push_back(0xF7);
  gRtpPackets.clear();
  e.add(note, 3, 5, sinkRtp, nullptr);
  e.add(sysex.data(), sysex.size(), 5, sinkRtp, nullptr);
  e.add(note, 3, 5, sinkRtp, nullptr);
  e.flush(sinkRtp, nullptr);
  CHECK(gRtpPackets.size() >= 3);
  RtpMidiDecoder d;
  gReceived.clear();
  uint16_t seq = gRtpPackets[0][2] << 8 | gRtpPackets[0][3];
  for (auto &pk : gRtpPackets) {
    CHECK(pk.size() <= RtpMidiEncoder::kMaxPacket);
    CHECK(get16(&pk[2]) == seq++);
    d.decode(pk.data() + 12, pk.size() - 12, 0, record);
  }
  CHECK(gReceived.size() == 3);
  CHECK(gReceived[0] == "90 3C 64 " && gReceived[2] == "90 3C 64 ");
  CHECK(gReceived[1] == hex(sysex.data(), sysex.size()));

  // SysEx lengths around the packet size
  for (size_t n : {490, 495, 496, 497, 498, 520, 1022}) {
    std::vector<uint8_t> s = {0xF0};
    for (size_t i = 0; i < n; i++) s.push_back(0x33);
    s.push_back(0xF7);
    gRtpPackets.clear();
    e.add(s.data(), s.size(), 0, sinkRtp, nullptr);
    e.flush(sinkRtp, nullptr);
    RtpMidiDecoder d2;
    gReceived.clear();
    for (auto &pk : gRtpPackets) {
      CHECK(pk.size() <= RtpMidiEncoder::kMaxPacket);
      d2.decode(pk.data() + 12, pk.size() - 12, 0, record);
    }
    CHECK(gReceived.size() == 1 && gReceived[0] == hex(s.data(), s.size()));
  }
}

struct Sent
{
  bool dataPort;
  AppleMidiSession::Endpoint to;
  std::vector<uint8_t> bytes;
};
static std::vector<Sent> gSent;
static void sendUdp(void *, bool dataPort, AppleMidiSession::Endpoint to, const uint8_t *data, size_t length)
{
  gSent.push_back({dataPort, to, std::vector<uint8_t>(data, data + length)});
}

static std::vector<uint8_t> invitation(const char *command, uint32_t token, uint32_t ssrc, const char *name)
{
  std::vector<uint8_t> p = {0xFF, 0xFF, (uint8_t)command[0], (uint8_t)command[1], 0, 0, 0, 2};
  p.resize(16);
  put32(&p[8], token);
  put32(&p[12], ssrc);
  for (const char *c = name; *c; c++) p.push_back(*c);
  p.push_back(0);
  return p;
}

static std::vector<uint8_t> rtpPacket(uint16_t seq, uint32_t ssrc, std::vector<uint8_t> list)
{
  std::vector<uint8_t> p(12);
  p[0] = 0x80;
  p[1] = 0x61;
  put16(&p[2], seq);
  put32(&p[8], ssrc);
  std::vector<uint8_t> s = section(list);
  p.insert(p.end(), s.begin(), s.end());
  return p;
}

TEST(testAppleMidiSession)
{
  AppleMidiSession s;
  s.begin("OxiMate", 0xABCD0001, sendUdp, nullptr);
  AppleMidiSession::Endpoint mac{0x0A000002, 5004}, macData{0x0A000002, 5005};
  const uint32_t peer = 0x55667788;
  gSent.clear();
  gReceived.clear();

  // Data before a session: ignored; data-port invitation before control: refused
  std::vector<uint8_t> note = rtpPacket(1, peer, {0x90, 60, 100});
  s.onData(note.data(), note.size(), macData, 0, 0, 0, record);
  CHECK(gReceived.empty());
  std::vector<uint8_t> in = invitation("IN", 42, peer, "MacBook");
  s.onData(in.data(), in.size(), macData, 0, 0, 0, record);
  CHECK(gSent.size() == 1 && gSent[0].bytes[2] == 'N' && gSent[0].bytes[3] == 'O');

  // Invitation on the control port, then the data port: OK with our name, from the same port
  gSent.clear();
  s.onControl(in.data(), in.size(), mac, 0, 100);
  CHECK(gSent.size() == 1 && !gSent[0].dataPort && gSent[0].to.port == 5004);
  CHECK(gSent[0].bytes[2] == 'O' && gSent[0].bytes[3] == 'K' && get32(&gSent[0].bytes[8]) == 42);
  CHECK(get32(&gSent[0].bytes[12]) == 0xABCD0001);
  CHECK(std::string((const char *)&gSent[0].bytes[16]) == "OxiMate");
  CHECK(!s.connected() && std::string(s.peerName()) == "MacBook");
  s.onData(in.data(), in.size(), macData, 0, 100, 0, record);
  CHECK(gSent.size() == 2 && gSent[1].dataPort && gSent[1].to.port == 5005 && gSent[1].bytes[3] == 'K');
  CHECK(s.connected());

  // A second host is refused while the session runs
  std::vector<uint8_t> other = invitation("IN", 7, 0x99, "iPad");
  s.onControl(other.data(), other.size(), {0x0A000003, 5004}, 0, 200);
  CHECK(gSent.size() == 3 && gSent[2].bytes[3] == 'O' && s.connected());

  // MIDI arrives; a gap in the sequence numbers counts lost packets; duplicates are dropped
  s.onData(note.data(), note.size(), macData, 0, 300, 5, record);
  CHECK(gReceived.size() == 1 && gReceived[0] == "90 3C 64 ");
  std::vector<uint8_t> later = rtpPacket(4, peer, {0x80, 60, 0});
  s.onData(later.data(), later.size(), macData, 0, 300, 5, record);
  s.onData(later.data(), later.size(), macData, 0, 300, 5, record);
  CHECK(gReceived.size() == 2 && s.lostPackets() == 2 && s.receivedPackets() == 2);
  std::vector<uint8_t> stranger = rtpPacket(5, 0x99, {0x80, 61, 0});
  s.onData(stranger.data(), stranger.size(), macData, 0, 300, 5, record);
  CHECK(gReceived.size() == 2);

  // Clock sync: answer count 0 with count 1 and our time; count 2 gives the latency
  gSent.clear();
  std::vector<uint8_t> ck(36, 0);
  ck[0] = ck[1] = 0xFF;
  ck[2] = 'C';
  ck[3] = 'K';
  put32(&ck[4], peer);
  put64(&ck[12], 1000);
  s.onData(ck.data(), ck.size(), macData, 5555, 400, 0, record);
  CHECK(gSent.size() == 1 && gSent[0].dataPort && gSent[0].bytes[8] == 1);
  CHECK(get32(&gSent[0].bytes[4]) == 0xABCD0001 && get64(&gSent[0].bytes[12]) == 1000 &&
        get64(&gSent[0].bytes[20]) == 5555);
  ck[8] = 2;
  put64(&ck[28], 1040);
  s.onData(ck.data(), ck.size(), macData, 5560, 400, 0, record);
  CHECK(s.latency100us() == 20);

  // Receiver feedback once a second, on the control port, with the last sequence number
  gSent.clear();
  s.poll(1500);
  CHECK(gSent.size() == 1 && !gSent[0].dataPort && gSent[0].bytes[2] == 'R' && get16(&gSent[0].bytes[8]) == 4);
  s.poll(1600);
  CHECK(gSent.size() == 1); // nothing new received

  // Sending goes to the data port
  gSent.clear();
  uint8_t packet[] = {1, 2, 3};
  s.sendData(packet, 3);
  CHECK(gSent.size() == 1 && gSent[0].dataPort && gSent[0].to.ip == macData.ip && gSent[0].to.port == 5005);

  // BY ends the session; then a new host can start one
  std::vector<uint8_t> by = invitation("BY", 0, peer, "");
  by.resize(16);
  s.onControl(by.data(), by.size(), mac, 0, 2000);
  CHECK(!s.connected() && s.peerName()[0] == 0);
  gSent.clear();
  s.sendData(packet, 3);
  CHECK(gSent.empty());
  s.onControl(other.data(), other.size(), {0x0A000003, 5004}, 0, 2100);
  CHECK(gSent.size() == 1 && gSent[0].bytes[3] == 'K');

  // No clock sync for a minute: the peer is gone
  s.onData(other.data(), other.size(), {0x0A000003, 5005}, 0, 2100, 0, record);
  CHECK(s.connected());
  s.poll(2100 + AppleMidiSession::kTimeoutMillis + 1);
  CHECK(!s.connected());
}
