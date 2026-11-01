// Host unit tests for the hardware-independent parts of OxiMate.
// Build and run with `make test` (uses the stubs in test/stubs).
#include <atomic>
#include <cstdio>
#include <string>
#include <vector>

#include <usbh_midi.h> // fake device: records sends in gUsbSent

#define private public // white-box access to parsers and internal state
#include "din_midi.h"
#include "midi_outputs.h"
#include "midi_task.h"
#include "note_tracker.h"
#include "router.h"
#include "usb_midi.h"
#undef private

uint32_t gFakeMillis = 0;
uint32_t gFakeMicros = 0;

static int gFailures = 0;
static int gChecks = 0;
#define CHECK(cond)                                                     \
  do {                                                                  \
    gChecks++;                                                          \
    if (!(cond)) {                                                      \
      gFailures++;                                                      \
      printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);            \
    }                                                                   \
  } while (0)

static std::vector<std::string> gReceived;
static std::vector<uint32_t> gReceivedTimes;

static std::string hex(const uint8_t *bytes, size_t n)
{
  std::string s;
  char b[4];
  for (size_t i = 0; i < n; i++) {
    snprintf(b, sizeof(b), "%02X ", bytes[i]);
    s += b;
  }
  return s;
}

static void record(const MidiMessage &m)
{
  std::string s = hex(m.bytes, m.length);
  if (m.cable) s += "c" + std::to_string(m.cable);
  gReceived.push_back(s);
  gReceivedTimes.push_back(m.timeMicros);
}

static void expect(const char *name, std::vector<std::string> expected)
{
  gChecks++;
  if (gReceived != expected) {
    gFailures++;
    printf("FAIL %s\n  got:", name);
    for (auto &s : gReceived) printf(" [%s]", s.c_str());
    printf("\n");
  }
  gReceived.clear();
  gReceivedTimes.clear();
}

static MidiMessage msg(MidiSource source, std::initializer_list<uint8_t> bytes)
{
  static uint8_t buf[16];
  size_t n = 0;
  for (uint8_t b : bytes) buf[n++] = b;
  return {source, 0, buf, (uint16_t)n};
}

static void testDinParser()
{
  HardwareSerial serial;
  DinMidi din;
  din.begin(serial, 0, 0, record);
  auto feed = [&](std::vector<uint8_t> bytes) {
    for (uint8_t b : bytes) din.parse(b, gFakeMicros++);
  };
  feed({0x90, 60, 100, 62, 90, 0x80, 60, 0});
  expect("running status", {"90 3C 64 ", "90 3E 5A ", "80 3C 00 "});
  feed({0x90, 60, 0xF8, 100});
  expect("realtime inside message", {"F8 ", "90 3C 64 "});
  feed({0xC0, 5, 6});
  expect("program change running status", {"C0 05 ", "C0 06 "});
  feed({0xF0, 0x7E, 0x01, 0xF8, 0x02, 0xF7});
  expect("SysEx with clock inside", {"F8 ", "F0 7E 01 02 F7 "});
  feed({0xF2, 1, 2, 3});
  expect("system common has no running status", {"F2 01 02 "});
  feed({0xF0, 1, 2, 0x90, 60, 1});
  expect("status byte aborts SysEx", {"90 3C 01 "});
  feed({0xF6});
  expect("tune request", {"F6 "});

  // Timestamps: a message is stamped with its status byte, running status with its first data byte
  gFakeMicros = 1000;
  feed({0x90, 60, 100}); // status at 1000
  feed({61, 100});       // running status, first data byte at 1003
  CHECK(gReceivedTimes.size() == 2 && gReceivedTimes[0] == 1000 && gReceivedTimes[1] == 1003);
  gReceived.clear();
  gReceivedTimes.clear();
}

static void testDinOutputPriority()
{
  HardwareSerial serial;
  DinMidi din;
  din.begin(serial, 0, 0, record);

  // A SysEx burst is queued, then a clock byte: the clock goes out next, not after the SysEx
  std::vector<uint8_t> sysex = {0xF0};
  for (int i = 0; i < 100; i++) sysex.push_back(0x11);
  sysex.push_back(0xF7);
  MidiMessage big = {MidiSource::Usb, 0, sysex.data(), (uint16_t)sysex.size()};
  din.send(big);
  din.pumpTx();
  CHECK(serial.out.size() == DinMidi::kFifoTarget); // only a few bytes go to the FIFO at once
  uint8_t clock = 0xF8;
  din.send({MidiSource::Usb, 0, &clock, 1});
  serial.drain(2);
  din.pumpTx();
  CHECK(serial.out.size() == DinMidi::kFifoTarget + 2 && serial.out[DinMidi::kFifoTarget] == 0xF8);

  // Everything still arrives, in order apart from the clock byte
  for (int i = 0; i < 200; i++) {
    serial.drain(4);
    din.pumpTx();
  }
  CHECK(serial.out.size() == sysex.size() + 1);
  CHECK(serial.out.back() == 0xF7);

  // Messages that don't fit the queue are dropped whole, never split
  DinMidi small;
  HardwareSerial serial2;
  small.begin(serial2, 0, 0, record);
  std::vector<uint8_t> huge(1000, 0x11);
  huge[0] = 0xF0;
  huge.back() = 0xF7;
  MidiMessage hugeMsg = {MidiSource::Usb, 0, huge.data(), (uint16_t)huge.size()};
  small.send(hugeMsg);
  small.send(hugeMsg);
  small.send(hugeMsg); // third one doesn't fit in 2047 bytes
  CHECK(small.counters().txDropped == 1);
}

static void testUsbParser()
{
  UsbMidi usb;
  usb.begin(record);
  auto packet = [&](std::vector<uint8_t> p) { usb.parsePacket(p.data(), 500); };
  packet({0x09, 0x90, 60, 100});
  packet({0x1B, 0xB1, 7, 127});
  packet({0x0C, 0xC2, 5, 0});
  packet({0x0F, 0xF8, 0, 0});
  expect("USB channel and realtime", {"90 3C 64 ", "B1 07 7F c1", "C2 05 ", "F8 "});
  packet({0x04, 0xF0, 1, 2});
  packet({0x04, 3, 4, 5});
  packet({0x06, 6, 0xF7, 0});
  expect("USB SysEx across packets", {"F0 01 02 03 04 05 06 F7 "});
  packet({0x04, 0xF0, 1, 2});
  packet({0x05, 0xF7, 0, 0});
  expect("USB SysEx ending with CIN 5", {"F0 01 02 F7 "});
  packet({0x07, 0xF0, 9, 0xF7});
  expect("USB short SysEx", {"F0 09 F7 "});
  packet({0x04, 3, 4, 5});
  packet({0x06, 6, 0xF7, 0});
  expect("USB continuation without start is dropped", {});
  packet({0x04, 0xF0, 1, 2});
  for (int i = 0; i < 400; i++) packet({0x04, 1, 1, 1});
  packet({0x06, 6, 0xF7, 0});
  expect("USB SysEx overflow is dropped", {});
  packet({0x07, 0xF0, 9, 0xF7});
  expect("USB recovers after overflow", {"F0 09 F7 "});
}

static void testUsbSendQueue()
{
  UsbMidi usb;
  usb.begin(record);
  uint8_t note[3] = {0x90, 60, 100};
  usb.send({MidiSource::Din, 0, note, 3});
  CHECK(usb.counters().txDropped == 0);
  CHECK(gUsbSent.empty()); // not connected yet: nothing queued

  gUsbSent.clear();
  usb.poll(); // fake device is connected; the first poll notices
  CHECK(usb.connected() && usb.vendorId() == 0x1234);
  usb.send({MidiSource::Din, 0, note, 3});
  uint8_t sysex[5] = {0xF0, 1, 2, 3, 0xF7};
  usb.send({MidiSource::Din, 0, sysex, 5});
  CHECK(gUsbSent.empty()); // queued until the next poll holds the SPI bus
  usb.poll();
  CHECK(gUsbSent.size() == 2);
  CHECK(gUsbSent.size() == 2 && gUsbSent[0][0] == 0x90 && gUsbSent[1].size() == 5 && gUsbSent[1][4] == 0xF7);
  gUsbSent.clear();
}

struct OutputRig
{
  HardwareSerial serial;
  DinMidi din;
  UsbMidi usb;
  NoteTracker tracker;
  MidiOutputs outputs;
  Router router;

  OutputRig()
  {
    din.begin(serial, 0, 0, record);
    usb.begin(record);
    outputs.begin(din, usb, tracker);
    router.begin(outputs);
  }
  std::vector<uint8_t> dinBytes()
  {
    for (int i = 0; i < 100; i++) {
      serial.drain(128);
      din.pumpTx();
    }
    std::vector<uint8_t> out = serial.out;
    serial.out.clear();
    return out;
  }
};

static void testRouter()
{
  OutputRig rig;
  Router &r = rig.router;

  MidiMessage note = msg(MidiSource::Usb, {0x92, 60, 100});
  CHECK(r.route(note));
  CHECK(rig.dinBytes() == std::vector<uint8_t>({0x92, 60, 100}));

  r.config().channelMask[0] &= ~(1 << 2);
  r.changed();
  CHECK(!r.route(note) && rig.dinBytes().empty());

  r.config().channelMask[0] = 0xFFFF;
  r.config().forceChannel[0] = 10;
  r.changed();
  CHECK(r.route(note));
  CHECK(rig.dinBytes()[0] == 0x99);
  CHECK(rig.tracker.isHeld(MidiOutput::Din, 9, 60)); // tracked on the channel actually sent

  // Edits to config() don't reach the MIDI side until changed()
  r.config().usbToDin = false;
  CHECK(r.route(note));
  r.changed();
  CHECK(!r.route(note));

  uint8_t clock = 0xF8;
  MidiMessage tick = {MidiSource::Din, 0, &clock, 1};
  r.config().clock = ClockSource::Usb;
  r.changed();
  CHECK(!r.route(tick));
  r.config().clock = ClockSource::Din;
  r.changed();
  CHECK(r.route(tick));

  MidiMessage sysex = msg(MidiSource::Din, {0xF0, 1, 0xF7});
  r.config().sysex = false;
  r.changed();
  CHECK(!r.route(sysex));

  r.resetDefaults();
  CHECK(r.config().usbToDin && r.config().channelMask[1] == 0xFFFF);

  // DIN -> USB only once a device is connected
  rig.dinBytes();
  MidiMessage dinNote = msg(MidiSource::Din, {0x90, 64, 1});
  CHECK(r.route(dinNote)); // DIN -> DIN still counts
  CHECK(!rig.tracker.isHeld(MidiOutput::Usb, 0, 64));
  rig.usb.poll();
  CHECK(r.route(dinNote));
  CHECK(rig.tracker.isHeld(MidiOutput::Usb, 0, 64));
}

static void testNoteTracker()
{
  OutputRig rig;
  NoteTracker &t = rig.tracker;
  Origin usbIn = Origin::fromInput(MidiSource::Usb);
  Origin drums = Origin::internal(5);
  auto send = [&](std::initializer_list<uint8_t> bytes, Origin origin = Origin::fromInput(MidiSource::Usb)) {
    rig.outputs.send(MidiOutput::Din, msg(MidiSource::Usb, bytes), origin);
  };

  send({0x90, 60, 100});
  CHECK(t.isHeld(MidiOutput::Din, 0, 60) && t.heldCount(MidiOutput::Din) == 1);
  send({0x90, 60, 0}); // velocity 0 = note-off
  CHECK(!t.isHeld(MidiOutput::Din, 0, 60) && t.heldCount(MidiOutput::Din) == 0);

  send({0x90, 61, 100});
  send({0x90, 61, 100}); // double note-on: still one note
  CHECK(t.heldCount(MidiOutput::Din) == 1);
  send({0x80, 61, 0});
  CHECK(t.heldCount(MidiOutput::Din) == 0);

  // Sustain: released notes stay sounding until the pedal goes up
  send({0xB0, 64, 127});
  send({0x90, 62, 100});
  send({0x80, 62, 0});
  CHECK(t.isHeld(MidiOutput::Din, 0, 62) && t.sustainMask(MidiOutput::Din) == 1);
  send({0xB0, 64, 0});
  CHECK(!t.isHeld(MidiOutput::Din, 0, 62) && t.sustainMask(MidiOutput::Din) == 0);

  // All notes off clears the channel
  send({0x93, 70, 100});
  send({0x93, 71, 100});
  send({0xB3, 123, 0});
  CHECK(t.heldCount(MidiOutput::Din) == 0);

  // Release by origin only touches that origin's notes, and sends real note-offs
  rig.dinBytes();
  send({0x99, 36, 100}, drums);
  send({0x90, 48, 100}, usbIn);
  rig.dinBytes();
  t.release(rig.outputs, MidiOutput::Din, &drums);
  CHECK(rig.dinBytes() == std::vector<uint8_t>({0x89, 36, 0}));
  CHECK(!t.isHeld(MidiOutput::Din, 9, 36) && t.isHeld(MidiOutput::Din, 0, 48));

  // Releasing everything also lifts held pedals
  send({0xB0, 64, 127});
  rig.dinBytes();
  t.release(rig.outputs, MidiOutput::Din);
  CHECK(rig.dinBytes() == std::vector<uint8_t>({0x80, 48, 0, 0xB0, 64, 0}));
  CHECK(t.heldCount(MidiOutput::Din) == 0 && t.sustainMask(MidiOutput::Din) == 0);

  // Stuck notes: the oldest note past the threshold
  gFakeMillis = 1000;
  send({0x90, 50, 100});
  gFakeMillis = 5000;
  send({0x90, 51, 100});
  MidiOutput out;
  NoteTracker::HeldNote stuck;
  CHECK(!t.findStuck(20000, 30000, out, stuck));
  CHECK(t.findStuck(40000, 30000, out, stuck) && stuck.note == 50 && out == MidiOutput::Din);
  t.clear(MidiOutput::Din);
  CHECK(t.heldCount(MidiOutput::Din) == 0);

  // More notes than slots: the bitsets stay right; extra notes lose age and origin only
  for (int ch = 0; ch < 2; ch++) {
    for (int n = 0; n < 100; n++) send({(uint8_t)(0x90 | ch), (uint8_t)n, 100});
  }
  CHECK(t.heldCount(MidiOutput::Din) == 200);
  t.release(rig.outputs, MidiOutput::Din);
  CHECK(t.heldCount(MidiOutput::Din) == 0);
}

static void testEventRing()
{
  static EventRing ring;
  uint8_t note[3] = {0x90, 60, 100};
  MidiMessage m = {MidiSource::Din, 0, note, 3, 1234};
  ring.push(m, true);
  MidiEvent e;
  CHECK(ring.pop(e) && e.timeMicros == 1234 && e.forwarded && e.count == 3 && e.bytes[1] == 60);
  CHECK(!ring.pop(e));

  std::vector<uint8_t> sysex(40, 0x22);
  sysex[0] = 0xF0;
  ring.push({MidiSource::Usb, 0, sysex.data(), (uint16_t)sysex.size()}, false);
  CHECK(ring.pop(e) && e.length == 40 && e.count == MidiEvent::kMaxBytes && !e.forwarded);

  for (size_t i = 0; i < EventRing::kLength + 10; i++) ring.push(m, true);
  CHECK(ring.dropped() == 11); // one slot always stays free
}

int main()
{
  testDinParser();
  testDinOutputPriority();
  testUsbParser();
  testUsbSendQueue();
  testRouter();
  testNoteTracker();
  testEventRing();
  printf("%d checks, %d failures\n", gChecks, gFailures);
  return gFailures ? 1 : 0;
}
