// Host unit tests for the hardware-independent parts of OxiMate.
// Build and run with `make test` (uses the stubs in test/stubs).
#include "test_support.h"

uint32_t gFakeMillis = 0;
uint32_t gFakeMicros = 0;
uint64_t gFakeMicros64 = 0;

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

static void testRouter()
{
  OutputRig rig;
  Router &r = rig.router;

  MidiMessage note = msg(MidiSource::Usb, {0x92, 60, 100});
  CHECK(r.route(note));
  CHECK(rig.dinBytes() == std::vector<uint8_t>({0x92, 60, 100}));

  r.config().channelMask[0] &= ~(1 << 2);
  r.changed();
  CHECK(!r.route(note));
  // The note held from before is released when its channel gets blocked
  CHECK(rig.dinBytes() == std::vector<uint8_t>({0x82, 60, 0}));

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

  // Clock goes to the clock engine, which ignores it unless it follows that input
  uint8_t clock = 0xF8;
  MidiMessage tick = {MidiSource::Din, 0, &clock, 1};
  CHECK(!r.route(tick));
  rig.clock.config().mode = ClockMode::FollowDin;
  rig.clock.changed();
  rig.clock.runDue(0);
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
  gFakeMicros += 100000; // later than the loop-protection window: not an echo of the first one
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

static void testClockInternal()
{
  OutputRig rig;
  gRunRig = &rig;
  ClockEngine &clock = rig.clock;
  ClockLog log;
  clock.addListener(ClockLog::listener, &log);
  ClockConfig &c = clock.config();
  CHECK(c.mode == ClockMode::Internal && !c.outputs[0].enabled && c.outputs[1].enabled); // USB by default
  c.outputs[0] = {true, true, 1};
  clock.changed();

  // Continuous clock while stopped: 48 ticks per second at 120 BPM, no musical events
  runFor(clock, 1000000);
  CHECK(countByte(rig.dinBytes(), 0xF8) == 47); // the 48th tick is due just after 1 s
  CHECK(log.count(ClockEvent::Step) == 0 && log.count(ClockEvent::Beat) == 0 && log.count(ClockEvent::Start) == 0);
  CHECK(log.count(ClockEvent::RawTick) == 47); // RawTick also while stopped (stage 22)

  // Start: FA, then beat 1 one millisecond later; 8 steps and 2 beats per second
  clock.start();
  runFor(clock, 1000000);
  std::vector<uint8_t> out = rig.dinBytes();
  CHECK(!out.empty() && out[0] == 0xFA);
  CHECK(log.count(ClockEvent::Start) == 1 && log.count(ClockEvent::Step) == 8 && log.count(ClockEvent::Beat) == 2);
  CHECK(log.count(ClockEvent::Bar) == 1);
  ClockEngine::Snapshot snap = clock.snapshot();
  CHECK(snap.running && snap.beat == 1 && snap.bar == 0 && snap.bpm == 120.0f);

  // Ticks stay on the 20833.3 us grid without drift
  std::vector<uint64_t> ticks = log.times(ClockEvent::Tick);
  CHECK(ticks.size() == 48);
  CHECK(ticks.size() == 48 && (ticks[47] - ticks[0]) / 47.0 > 20833.0 && (ticks[47] - ticks[0]) / 47.0 < 20834.0);

  // Dividers and multipliers per output
  c.outputs[0].ratio = -2;
  clock.changed();
  runFor(clock, 1000000);
  CHECK(countByte(rig.dinBytes(), 0xF8) == 24);
  c.outputs[0].ratio = 3;
  clock.changed();
  runFor(clock, 1000000);
  int multiplied = countByte(rig.dinBytes(), 0xF8);
  CHECK(multiplied >= 143 && multiplied <= 144);

  // Stop: FC; clock keeps running but steps stop
  c.outputs[0].ratio = 1;
  clock.changed();
  clock.stop();
  log.events.clear();
  runFor(clock, 500000);
  out = rig.dinBytes();
  CHECK(!out.empty() && out[0] == 0xFC && countByte(out, 0xF8) == 24);
  CHECK(log.count(ClockEvent::Stop) == 1 && log.count(ClockEvent::Step) == 0);

  // Transport off for an output: no FA there, ticks still go
  c.outputs[0].transport = false;
  clock.changed();
  clock.start();
  runFor(clock, 100000);
  out = rig.dinBytes();
  CHECK(countByte(out, 0xFA) == 0 && countByte(out, 0xF8) > 0);

  // Tempo change takes effect: 60 BPM = 24 ticks per second
  c.bpm = 60;
  clock.changed();
  runFor(clock, 100000);
  rig.dinBytes();
  runFor(clock, 1000000);
  CHECK(countByte(rig.dinBytes(), 0xF8) == 24);
}

static void testClockSwing()
{
  OutputRig rig;
  gRunRig = &rig;
  ClockEngine &clock = rig.clock;
  ClockLog log;
  clock.addListener(ClockLog::listener, &log);
  clock.config().swing = 75;
  clock.changed();
  clock.start();
  runFor(clock, 500000);
  std::vector<uint64_t> steps = log.times(ClockEvent::Step);
  double tick = 60e6 / (24 * 120.0);
  // 16th swing at 75%: odd steps move 3 ticks later (9 ticks after the even step), even steps stay put
  CHECK(steps.size() >= 4);
  if (steps.size() >= 4) {
    CHECK(fabs((double)(steps[1] - steps[0]) - 9 * tick) < 2);
    CHECK(fabs((double)(steps[2] - steps[0]) - 12 * tick) < 2);
  }

  // 8th swing: the step on the off-beat 8th moves later
  log.events.clear();
  clock.config().swingGrid = 8;
  clock.changed();
  clock.stop();
  clock.start();
  runFor(clock, 500000);
  steps = log.times(ClockEvent::Step);
  CHECK(steps.size() >= 4);
  if (steps.size() >= 4) {
    CHECK(fabs((double)(steps[2] - steps[0]) - 18 * tick) < 2); // 12 ticks + 0.25 * 24
  }
}

static void testClockFollow()
{
  OutputRig rig;
  gRunRig = &rig;
  ClockEngine &clock = rig.clock;
  ClockLog log;
  clock.addListener(ClockLog::listener, &log);
  ClockConfig &c = clock.config();
  c.mode = ClockMode::FollowDin;
  c.outputs[0] = {true, true, 1};
  clock.changed();
  clock.runDue(0);

  uint8_t bytes[1];
  auto send = [&](MidiSource source, uint8_t status) {
    bytes[0] = status;
    MidiMessage m = {source, 0, bytes, 1, (uint32_t)gFakeMicros64};
    return rig.router.route(m);
  };

  CHECK(!send(MidiSource::Usb, 0xF8)); // not the followed input
  CHECK(rig.dinBytes().empty());

  CHECK(send(MidiSource::Din, 0xFA));
  for (int i = 0; i < 30; i++) {
    gFakeMicros64 += 20833;
    CHECK(send(MidiSource::Din, 0xF8));
    clock.runDue(gFakeMicros64);
  }
  std::vector<uint8_t> out = rig.dinBytes();
  CHECK(out.size() == 31 && out[0] == 0xFA && countByte(out, 0xF8) == 30); // passed straight on
  ClockEngine::Snapshot snap = clock.snapshot();
  CHECK(snap.locked && snap.running && fabs(snap.bpm - 120.0f) < 0.1f);
  CHECK(log.count(ClockEvent::Step) == 5);

  // An outlier interval while locked is ignored for the tempo
  gFakeMicros64 += 5000;
  send(MidiSource::Din, 0xF8);
  clock.runDue(gFakeMicros64);
  CHECK(fabs(clock.snapshot().bpm - 120.0f) < 0.5f);

  // Lost: no tick for 500 ms -> stop, FC sent (decided 2026-11-01)
  rig.dinBytes();
  runFor(clock, 600000);
  snap = clock.snapshot();
  CHECK(snap.lost && !snap.running && !snap.locked);
  out = rig.dinBytes();
  CHECK(out.size() == 1 && out[0] == 0xFC);

  // Transport buttons don't fight a followed clock
  clock.start();
  clock.runDue(gFakeMicros64);
  CHECK(!clock.snapshot().running);
}

static void testTapTempo()
{
  // No tempo before the 4th tap
  TapTempo tap;
  CHECK(tap.tap(0) == 0);
  CHECK(tap.tap(500000) == 0);
  CHECK(tap.tap(1000000) == 0 && tap.taps() == 3);
  CHECK(fabsf(tap.tap(1500000) - 120) < 0.01f);
  CHECK(tap.tap(1600000) == 0);                     // stray double tap ignored
  CHECK(fabsf(tap.tap(2000000) - 120) < 0.01f);
  CHECK(fabsf(tap.tap(2600000) - 60e6f / 525000) < 0.01f); // average of 500, 500, 500, 600 ms

  // A missed tap (a gap of two intervals) is filled in instead of breaking the run
  TapTempo missed;
  missed.tap(0);
  missed.tap(500000);
  missed.tap(1000000);                              // 3 taps, no tempo yet
  CHECK(fabsf(missed.tap(2000000) - 120) < 0.01f);  // 1500 didn't register: counts as 2 intervals
  CHECK(missed.taps() == 5);
  CHECK(fabsf(missed.tap(2500000) - 120) < 0.01f);

  // A pause over 3 s starts a new run; at 60 BPM a missed tap (2 s) does not
  TapTempo slow;
  slow.tap(0);
  slow.tap(1000000);
  CHECK(fabsf(slow.tap(3000000) - 60) < 0.01f && slow.taps() == 4); // 0, 1, (2 filled in), 3 = 4 taps
  CHECK(fabsf(slow.tap(4000000) - 60) < 0.01f);
  CHECK(slow.tap(8000000) == 0 && slow.taps() == 1);  // over 3 s later: a new run
}

static void testRouterMigration()
{
  // Version 1 settings (with the old clock setting) become version 2, and report the clock
  gFakeNvs.clear();
  struct
  {
    uint8_t version;
    bool usbToDin, dinToDin, dinToUsb;
    uint16_t channelMask[2];
    uint8_t forceChannel[2];
    ClockSource clock;
    bool sysex;
    uint8_t ledLevel;
  } old;
  CHECK(sizeof(old) == 14);
  memset(&old, 0, sizeof(old));
  old = {1, true, false, true, {0x7FFF, 0xFFFF}, {3, 0}, ClockSource::Usb, true, 3};
  gFakeNvs["oximate/router"] = std::vector<uint8_t>((uint8_t *)&old, (uint8_t *)&old + sizeof(old));

  HardwareSerial serial;
  DinMidi din;
  UsbMidi usb;
  NoteTracker tracker;
  MidiOutputs outputs;
  ClockEngine clock;
  Router router;
  din.begin(serial, 0, 0, record);
  outputs.begin(din, usb, tracker);
  clock.begin(outputs);
  router.begin(outputs, clock);
  ClockSource migrated;
  CHECK(router.migratedClock(migrated) && migrated == ClockSource::Usb);
  const RouterConfig &c = router.config();
  CHECK(c.version == 2 && !c.dinToDin && c.channelMask[0] == 0x7FFF && c.forceChannel[0] == 3 && c.ledLevel == 3);

  clock.migrateFromRouter(ClockMode::FollowUsb, false);
  CHECK(clock.config().mode == ClockMode::FollowUsb);
}

static void testPanic()
{
  OutputRig rig;
  gRunRig = &rig;
  Panic panic;
  panic.begin(rig.outputs, rig.tracker, rig.usb, rig.clock);
  gFakeMillis = 1000;
  auto run = [&](int loops) {
    for (int i = 0; i < loops; i++) {
      panic.run(gFakeMillis);
      rig.serial.drain(128);
      rig.din.pumpTx();
    }
  };

  // Level 2: note-offs for held notes, Stop, then CC123/CC120/CC64 on all 16 channels
  rig.router.route(msg(MidiSource::Usb, {0x90, 60, 100}));
  rig.router.route(msg(MidiSource::Usb, {0x91, 62, 100}));
  rig.dinBytes();
  panic.request(2);
  run(5);
  std::vector<uint8_t> out = rig.dinBytes();
  CHECK(out.size() >= 6 + 1 + 48 * 3);
  CHECK(out.size() >= 7 && out[0] == 0xFC); // realtime goes first
  CHECK(out.size() >= 7 && out[1] == 0x80 && out[2] == 60 && out[4] == 0x81 && out[5] == 62);
  CHECK(countByte(out, 0xB0) == 3 && countByte(out, 0xBF) == 3);
  Panic::Summary state = panic.summary();
  CHECK(!state.active && state.panics == 1 && state.held[0] == 0);

  // Level 3: a note-off for every note on every channel, fed as the queue has room
  panic.request(3);
  run(1);
  CHECK(panic.summary().active);
  run(2000); // ~4 bytes per pump, like 1 ms on the device: level 3 takes about 2 s
  out = rig.dinBytes();
  CHECK(out.size() == 1 + 48 * 3 + 2048 * 3);
  CHECK(!panic.summary().active && panic.summary().panics == 2);

  // Active Sensing from DIN stops: its notes are released, USB notes stay
  rig.router.route(msg(MidiSource::Din, {0x90, 40, 100}));
  rig.router.route(msg(MidiSource::Usb, {0x90, 41, 100}));
  uint8_t sense = 0xFE;
  panic.observe({MidiSource::Din, 0, &sense, 1});
  gFakeMillis += 200;
  run(1);
  CHECK(rig.tracker.isHeld(MidiOutput::Din, 0, 40));
  gFakeMillis += 200;
  run(1);
  CHECK(!rig.tracker.isHeld(MidiOutput::Din, 0, 40) && rig.tracker.isHeld(MidiOutput::Din, 0, 41));

  // Long notes are never cut on a timer (long pads are played on purpose)
  gFakeMillis += 120000;
  rig.dinBytes();
  run(1);
  CHECK(rig.dinBytes().empty() && rig.tracker.isHeld(MidiOutput::Din, 0, 41));
  CHECK(panic.summary().autoReleased == 1);
}

static void testLoopProtection()
{
  OutputRig rig;
  gFakeMicros = 1000000;
  // A note we send to DIN OUT comes straight back on DIN IN (device with MIDI thru)
  CHECK(rig.router.route(msg(MidiSource::Usb, {0x90, 60, 100})));
  rig.dinBytes();
  gFakeMicros += 5000;
  MidiMessage echo = msg(MidiSource::Din, {0x90, 60, 100});
  echo.timeMicros = gFakeMicros;
  CHECK(!rig.router.route(echo)); // dropped: not sent out again
  CHECK(rig.dinBytes().empty() && rig.outputs.counters().echoesBlocked == 1);
  // The same note played on the DIN device later is a real note
  gFakeMicros += 100000;
  echo.timeMicros = gFakeMicros;
  CHECK(rig.router.route(echo));
  CHECK(rig.dinBytes() == std::vector<uint8_t>({0x90, 60, 100}));
}

static void testRouterReleasesOnChange()
{
  OutputRig rig;
  Router &r = rig.router;
  r.route(msg(MidiSource::Usb, {0x90, 60, 100}));
  rig.dinBytes();
  // Forcing USB to channel 5 while the note is held: its note-off goes to channel 1 first
  r.config().forceChannel[0] = 5;
  r.changed();
  r.applyPending();
  CHECK(rig.dinBytes() == std::vector<uint8_t>({0x80, 60, 0}));
  r.route(msg(MidiSource::Usb, {0x90, 61, 100}));
  CHECK(rig.dinBytes() == std::vector<uint8_t>({0x94, 61, 100}));
  // Changes that don't affect routing (LED level) leave held notes alone
  r.config().ledLevel = 1;
  r.changed();
  r.applyPending();
  CHECK(rig.dinBytes().empty() && rig.tracker.isHeld(MidiOutput::Din, 4, 61));
}

static void testSamSynth()
{
  // GS reset from the datasheet, with the Roland checksum
  CHECK(gsBytes(0x40, 0x00, 0x7F, 0x00) ==
        std::vector<uint8_t>({0xF0, 0x41, 0x00, 0x42, 0x12, 0x40, 0x00, 0x7F, 0x00, 0x41, 0xF7}));
  CHECK(SamSynth::defaultChannel(0) == 10 && SamSynth::defaultChannel(9) == 9 && SamSynth::defaultChannel(10) == 11);
  uint8_t ff = 0xFF;
  CHECK(SamSynth::isReset({MidiSource::Din, 0, &ff, 1}));
  uint8_t gm[6] = {0xF0, 0x7E, 0x7F, 0x09, 0x01, 0xF7};
  CHECK(SamSynth::isReset({MidiSource::Din, 0, gm, 6}));

  OutputRig rig;
  SamSynth synth;
  synth.begin(rig.outputs);
  gFakeMillis = 5000;
  synth.run(gFakeMillis);
  CHECK(rig.dinBytes().empty()); // All mode with defaults: nothing sent at boot

  // Layers mode: other parts muted, layer 1 (part 1) configured on channel 16, then on channel 1
  SynthConfig &c = synth.config();
  c.listen = SynthListen::Layers;
  synth.changed();
  synth.run(gFakeMillis);
  std::vector<uint8_t> out = rig.dinBytes();
  CHECK(contains(out, gsBytes(0x40, 0x1F, 0x02, 16)));         // part 15 off
  CHECK(contains(out, gsBytes(0x40, 0x11, 0x02, 15)));         // part 1 -> channel 16 (config)
  CHECK(contains(out, {0xCF, 0}));                             // its program on channel 16
  CHECK(contains(out, {0xBF, 7, 100}));                        // its volume on channel 16
  CHECK(contains(out, gsBytes(0x40, 0x11, 0x02, 0)));          // then part 1 -> channel 1
  CHECK(contains(out, gsBytes(0x40, 0x12, 0x02, 16)));         // layer 2 is off: part 2 muted
  CHECK(contains(out, gsBytes(0x40, 0x10, 0x15, 1)));          // drum layer part is a rhythm part
  CHECK(contains(out, {0xBF, 99, 0x37, 0xBF, 98, 0x07, 0xBF, 6, 127, 0xBF, 99, 127, 0xBF, 98, 127})); // master
  CHECK(!contains(out, {0xC0, 0}));                            // nothing on channel 1 except via the part map
  CHECK(out.size() < 700);

  // One layer edit re-sends only that layer
  c.layers[0].volume = 80;
  synth.changed();
  synth.run(gFakeMillis);
  out = rig.dinBytes();
  CHECK(contains(out, {0xBF, 7, 80}) && !contains(out, gsBytes(0x40, 0x1F, 0x02, 16)));

  // Test chord on layer 1: on channel 16, off after 700 ms, part back on channel 1
  synth.testLayer(0);
  synth.run(gFakeMillis);
  out = rig.dinBytes();
  CHECK(contains(out, {0x9F, 60, 100}) && contains(out, {0x9F, 67, 100}));
  gFakeMillis += 800;
  synth.run(gFakeMillis);
  out = rig.dinBytes();
  CHECK(contains(out, {0x8F, 60, 0}) && contains(out, gsBytes(0x40, 0x11, 0x02, 0)));

  // A System Reset passing through to DIN OUT: everything is re-applied 100 ms later
  rig.router.route(msg(MidiSource::Usb, {0xFF}));
  rig.dinBytes();
  synth.run(gFakeMillis);
  CHECK(rig.dinBytes().empty());
  gFakeMillis += 150;
  synth.run(gFakeMillis);
  CHECK(contains(rig.dinBytes(), gsBytes(0x40, 0x1F, 0x02, 16)));

  // All mode: part 15 (channel 16) is muted while a preview plays on the config channel
  c.listen = SynthListen::All;
  synth.changed();
  synth.run(gFakeMillis);
  rig.dinBytes();
  synth.preview(0, 0, 81);
  synth.run(gFakeMillis);
  out = rig.dinBytes();
  CHECK(contains(out, gsBytes(0x40, 0x1F, 0x02, 16)) && contains(out, {0xCF, 81}));
  gFakeMillis += 800;
  synth.run(gFakeMillis);
  out = rig.dinBytes();
  CHECK(contains(out, gsBytes(0x40, 0x1F, 0x02, 15)) && contains(out, gsBytes(0x40, 0x11, 0x02, 0)));
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
  testClockInternal();
  testClockSwing();
  testClockFollow();
  testTapTempo();
  testPanic();
  gRunRig = nullptr;
  testRouterReleasesOnChange();
  testLoopProtection();
  testSamSynth();
  testRouterMigration();
  for (const TestCase &test : testRegistry()) test.function(); // tests from test/test_*.cpp
  printf("%d checks, %d failures\n", gChecks, gFailures);
  return gFailures ? 1 : 0;
}
