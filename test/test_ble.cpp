// Tests for the BLE MIDI packet codec (stage 17)
#include <Arduino.h>
#include "test_support.h"
#include "ble_midi_codec.h"

static std::vector<MidiSource> gBleSources;
static void recordBle(const MidiMessage &m)
{
  gBleSources.push_back(m.source);
  record(m);
}

static void decode(BleMidiDecoder &d, std::vector<uint8_t> packet, uint32_t now = 0)
{
  d.decode(packet.data(), packet.size(), now, recordBle);
}

TEST(testBleDecodeBasic)
{
  BleMidiDecoder d;
  gReceived.clear();
  gReceivedTimes.clear();
  gBleSources.clear();
  // header (ts high 0x01), ts low 0x05, note on
  decode(d, {0x81, 0x85, 0x90, 60, 100}, 1234);
  CHECK(gReceived.size() == 1 && gReceived[0] == "90 3C 64 ");
  CHECK(gReceivedTimes[0] == 1234);
  CHECK(gBleSources[0] == MidiSource::Ble);
  CHECK(d.lastTimestamp() == ((1 << 7) | 5));

  // two messages with their own timestamps in one packet
  gReceived.clear();
  decode(d, {0x80, 0x81, 0x90, 60, 0, 0x82, 0xB3, 7, 127});
  CHECK(gReceived.size() == 2 && gReceived[0] == "90 3C 00 " && gReceived[1] == "B3 07 7F ");
  CHECK(d.lastTimestamp() == 2);

  // invalid header: dropped
  gReceived.clear();
  decode(d, {0x40, 0x81, 0x90, 60, 0});
  decode(d, {0xC0, 0x81, 0x90, 60, 0});
  CHECK(gReceived.empty() && d.badPackets() == 2);

  // program change and channel pressure: one data byte; system common
  decode(d, {0x80, 0x80, 0xC2, 5, 0x80, 0xD2, 64, 0x80, 0xF2, 1, 2, 0x80, 0xF6});
  CHECK(gReceived.size() == 4 && gReceived[0] == "C2 05 " && gReceived[1] == "D2 40 " &&
        gReceived[2] == "F2 01 02 " && gReceived[3] == "F6 ");
}

TEST(testBleDecodeRunningStatus)
{
  BleMidiDecoder d;
  gReceived.clear();
  // running status without timestamp bytes, then with a timestamp byte before the data
  decode(d, {0x80, 0x81, 0x90, 60, 100, 62, 101, 0x83, 64, 102});
  CHECK(gReceived.size() == 3);
  CHECK(gReceived[1] == "90 3E 65 " && gReceived[2] == "90 40 66 ");
  CHECK(d.lastTimestamp() == 3);

  // running status carries over to the next packet
  gReceived.clear();
  decode(d, {0x80, 0x84, 65, 0});
  CHECK(gReceived.size() == 1 && gReceived[0] == "90 41 00 ");

  // a real-time byte inside a message doesn't break it, or the running status
  gReceived.clear();
  decode(d, {0x80, 0x81, 0x91, 60, 0x82, 0xF8, 100, 61, 90});
  CHECK(gReceived.size() == 3 && gReceived[0] == "F8 " && gReceived[1] == "91 3C 64 " &&
        gReceived[2] == "91 3D 5A ");

  // system common cancels running status: stray data is ignored
  gReceived.clear();
  decode(d, {0x80, 0x81, 0xF3, 4, 60, 100});
  CHECK(gReceived.size() == 1 && gReceived[0] == "F3 04 ");

  // stray data before any status
  BleMidiDecoder fresh;
  gReceived.clear();
  decode(fresh, {0x80, 60, 100});
  CHECK(gReceived.empty());
}

TEST(testBleDecodeSysEx)
{
  BleMidiDecoder d;
  gReceived.clear();
  // SysEx in one packet
  decode(d, {0x80, 0x81, 0xF0, 0x7E, 0x7F, 0x06, 0x01, 0x82, 0xF7});
  CHECK(gReceived.size() == 1 && gReceived[0] == "F0 7E 7F 06 01 F7 ");

  // across three packets, with a clock byte in the middle
  gReceived.clear();
  decode(d, {0x80, 0x81, 0xF0, 0x41, 0x10});
  decode(d, {0x80, 0x42, 0x12, 0x83, 0xF8, 0x40});
  CHECK(gReceived.size() == 1 && gReceived[0] == "F8 ");
  decode(d, {0x80, 0x00, 0x7F, 0x84, 0xF7, 0x85, 0x90, 1, 2});
  CHECK(gReceived.size() == 3 && gReceived[1] == "F0 41 10 42 12 40 00 7F F7 " && gReceived[2] == "90 01 02 ");

  // a status byte cuts an unfinished SysEx (dropped)
  gReceived.clear();
  decode(d, {0x80, 0x81, 0xF0, 1, 2, 0x82, 0x93, 3, 4});
  CHECK(gReceived.size() == 1 && gReceived[0] == "93 03 04 " && d.droppedSysEx() == 1);

  // too long: dropped
  gReceived.clear();
  std::vector<uint8_t> start = {0x80, 0x81, 0xF0};
  decode(d, start);
  std::vector<uint8_t> body(200, 0x11);
  body.insert(body.begin(), 0x80);
  for (int i = 0; i < 6; i++) decode(d, body);
  decode(d, {0x80, 0x82, 0xF7});
  CHECK(gReceived.empty() && d.droppedSysEx() == 2);
}

static std::vector<std::vector<uint8_t>> gPackets;
static void sinkPacket(void *, const uint8_t *packet, size_t length)
{
  gPackets.push_back(std::vector<uint8_t>(packet, packet + length));
}

TEST(testBleEncode)
{
  BleMidiEncoder e;
  gPackets.clear();
  uint8_t note[] = {0x90, 60, 100}, cc[] = {0xB0, 1, 2};
  e.add(note, 3, 0x0105, sinkPacket, nullptr); // ts high 0x02, low 0x05
  e.add(cc, 3, 0x0106, sinkPacket, nullptr);
  CHECK(gPackets.empty() && e.pending());
  e.flush(sinkPacket, nullptr);
  CHECK(gPackets.size() == 1);
  CHECK(gPackets[0] == std::vector<uint8_t>({0x82, 0x85, 0x90, 60, 100, 0x86, 0xB0, 1, 2}));
  e.flush(sinkPacket, nullptr);
  CHECK(gPackets.size() == 1); // nothing pending

  // a new header when the high timestamp bits change
  gPackets.clear();
  e.add(note, 3, 0x007F, sinkPacket, nullptr);
  e.add(note, 3, 0x0080, sinkPacket, nullptr);
  e.flush(sinkPacket, nullptr);
  CHECK(gPackets.size() == 2 && gPackets[0][0] == 0x80 && gPackets[1][0] == 0x81 && gPackets[1][1] == 0x80);

  // a full packet goes out (20 bytes: header + 4 x 4 bytes, the fifth message starts a new packet)
  gPackets.clear();
  for (int i = 0; i < 5; i++) e.add(note, 3, 10, sinkPacket, nullptr);
  CHECK(gPackets.size() == 1 && gPackets[0].size() == 17);
  e.flush(sinkPacket, nullptr);
  CHECK(gPackets.size() == 2 && gPackets[1].size() == 5);
}

TEST(testBleEncodeSysExRoundTrip)
{
  BleMidiEncoder e;
  e.setMaxPacket(5);
  CHECK(e.maxPacket() == BleMidiEncoder::kMinPacket);
  e.setMaxPacket(1000);
  CHECK(e.maxPacket() == BleMidiEncoder::kMaxPacket);
  e.setMaxPacket(20);

  std::vector<uint8_t> sysex = {0xF0};
  for (int i = 0; i < 60; i++) sysex.push_back(i);
  sysex.push_back(0xF7);
  gPackets.clear();
  uint8_t note[] = {0x80, 60, 0};
  e.add(note, 3, 5, sinkPacket, nullptr);
  e.add(sysex.data(), sysex.size(), 5, sinkPacket, nullptr);
  e.add(note, 3, 6, sinkPacket, nullptr);
  e.flush(sinkPacket, nullptr);
  CHECK(gPackets.size() >= 4);
  for (auto &p : gPackets) CHECK(p.size() <= 20 && (p[0] & 0xC0) == 0x80);
  // continuation packets: data straight after the header
  CHECK(gPackets[1][1] < 0x80);

  BleMidiDecoder d;
  gReceived.clear();
  for (auto &p : gPackets) d.decode(p.data(), p.size(), 0, record);
  CHECK(gReceived.size() == 3);
  CHECK(gReceived[0] == "80 3C 00 ");
  CHECK(gReceived[1] == hex(sysex.data(), sysex.size()));
  CHECK(gReceived[2] == "80 3C 00 ");

  // the F7 needs its own timestamp: a SysEx filling a packet exactly still closes properly
  for (int extra = 0; extra < 25; extra++) {
    std::vector<uint8_t> s = {0xF0};
    for (int i = 0; i < extra; i++) s.push_back(0x55);
    s.push_back(0xF7);
    gPackets.clear();
    e.add(s.data(), s.size(), 77, sinkPacket, nullptr);
    e.flush(sinkPacket, nullptr);
    BleMidiDecoder d2;
    gReceived.clear();
    for (auto &p : gPackets) {
      CHECK(p.size() <= 20);
      d2.decode(p.data(), p.size(), 0, record);
    }
    CHECK(gReceived.size() == 1 && gReceived[0] == hex(s.data(), s.size()));
  }
}

TEST(testPacketRing)
{
  static PacketRing ring;
  uint8_t out[300];
  CHECK(ring.pop(out, sizeof(out)) == 0);
  uint8_t a[] = {1, 2, 3}, b[] = {4, 5};
  CHECK(ring.push(a, 3) && ring.push(b, 2));
  CHECK(ring.used() == 9);
  CHECK(ring.pop(out, sizeof(out)) == 3 && out[2] == 3);
  CHECK(ring.pop(out, sizeof(out)) == 2 && out[1] == 5);
  CHECK(ring.pop(out, sizeof(out)) == 0);

  // fill, wrap around, overflow
  uint8_t big[200];
  for (int i = 0; i < 200; i++) big[i] = i;
  int pushed = 0;
  while (ring.push(big, 200)) pushed++;
  CHECK(pushed == (int)(PacketRing::kBytes / 202));
  CHECK(ring.dropped() == 1);
  for (int round = 0; round < 50; round++) {
    CHECK(ring.pop(out, sizeof(out)) == 200 && out[199] == 199 && out[0] == 0);
    CHECK(ring.push(big, 200));
  }
}
