// Tests for the looper (stage 13): loop buffer, Standard MIDI Files, clock-synced recording
#include "test_support.h"

#include <Arduino.h>
#define private public
#include "looper.h"
#undef private
#include "smf.h"

static bool isOn(const LoopEvent &e) { return (e.status & 0xF0) == 0x90 && e.data2; }

TEST(testLoopBufferMerge)
{
  static LoopEvent events[64], pending[16];
  LoopBuffer buffer;
  buffer.attach(events, 64, pending, 16);
  buffer.setLength(384); // one bar of 4/4
  buffer.record(96, 0x90, 60, 100, 0, false);
  buffer.record(150, 0x80, 60, 0, 0, false);
  buffer.record(0, 0x90, 36, 110, 0, true);
  buffer.record(40, 0x80, 36, 0, 0, true);
  CHECK(buffer.count() == 0 && buffer.pendingCount() == 4); // nothing plays until the cycle ends
  CHECK(buffer.commit(0, nullptr, 0) == 0);
  CHECK(buffer.count() == 4 && buffer.pendingCount() == 0);
  CHECK(buffer[0].tick == 0 && buffer[1].tick == 40 && buffer[2].tick == 96 && buffer[3].tick == 150);
  CHECK(buffer[0].flags & 0x10); // recorded from DIN

  // A second pass merges in tick order; at equal ticks the earlier pass comes first
  buffer.record(96, 0x91, 64, 90, 1, false);
  buffer.record(200, 0x81, 64, 0, 1, false);
  buffer.commit(0, nullptr, 0);
  CHECK(buffer.count() == 6 && buffer.passes() == 2);
  CHECK(buffer[2].tick == 96 && buffer[2].data1 == 60 && buffer[3].tick == 96 && buffer[3].data1 == 64);
  CHECK(buffer.lowerBound(96) == 2 && buffer.lowerBound(97) == 4 && buffer.lowerBound(1000) == 6);

  // Undo removes exactly the last pass
  CHECK(buffer.undo() && buffer.count() == 4 && buffer.passes() == 1);
  CHECK(buffer.undo() && buffer.count() == 0);
  CHECK(!buffer.undo());
}

TEST(testLoopBufferHeldAndQuantise)
{
  static LoopEvent events[64], pending[16];
  LoopBuffer buffer;
  buffer.attach(events, 64, pending, 16);
  buffer.setLength(384);

  // A note still held at the loop end is cut there and reported for a restart
  buffer.record(300, 0x92, 48, 77, 0, false);
  LoopBuffer::HeldNote held[4];
  CHECK(buffer.commit(0, held, 4) == 1);
  CHECK(held[0].status == 0x92 && held[0].note == 48 && held[0].velocity == 77);
  CHECK(buffer.count() == 2 && buffer[1].tick == 383 && buffer[1].status == 0x82);

  // Quantise to 1/16 (24 ticks): the note-on moves to the grid, its length is kept
  buffer.clear();
  buffer.record(29, 0x90, 60, 100, 0, false);
  buffer.record(69, 0x80, 60, 0, 0, false);
  buffer.commit(24, nullptr, 0);
  CHECK(buffer.count() == 2 && buffer[0].tick == 24 && buffer[1].tick == 64);

  // Recording wraps positions into the loop
  buffer.clear();
  buffer.record(384 + 10, 0x90, 61, 1, 0, false);
  buffer.commit(0, nullptr, 0);
  CHECK(buffer[0].tick == 10);
}

TEST(testSmfRoundTrip)
{
  LoopEvent events[] = {{0, 0x99, 36, 110, 0},  {24, 0x89, 36, 0, 0}, {96, 0x90, 60, 100, 0},
                        {96, 0x91, 64, 90, 0},   {150, 0xB0, 74, 64, 0}, {180, 0x80, 60, 0, 0},
                        {200, 0x81, 64, 0, 0},   {300, 0xE0, 0, 72, 0},  {320, 0xD0, 50, 0, 0}};
  uint32_t n = sizeof(events) / sizeof(events[0]);
  Smf::Info info;
  info.bpm = 128;
  info.beatsPerBar = 4;
  info.lengthTicks = 384;
  for (int type1 = 0; type1 <= 1; type1++) {
    Smf::Sink size;
    size_t bytes = Smf::write(events, n, info, type1, size);
    std::vector<uint8_t> file(bytes);
    Smf::Sink sink;
    sink.data = file.data();
    sink.capacity = file.size();
    CHECK(Smf::write(events, n, info, type1, sink) == bytes && !sink.overflow);
    CHECK(file[0] == 'M' && file[9] == type1);

    LoopEvent back[32];
    uint32_t count = 0;
    Smf::Info read;
    CHECK(Smf::read(file.data(), file.size(), back, 32, count, read));
    CHECK(count == n);
    CHECK(fabsf(read.bpm - 128) < 0.01f && read.beatsPerBar == 4 && read.lengthTicks == 384);
    // Same messages at the same ticks (type 1 groups by channel, so compare as sorted sets)
    bool same = count == n;
    for (uint32_t i = 0; i < n && same; i++) {
      bool found = false;
      for (uint32_t j = 0; j < count; j++) {
        found |= back[j].tick == events[i].tick && back[j].status == events[i].status &&
                 back[j].data1 == events[i].data1 && back[j].data2 == events[i].data2;
      }
      same &= found;
    }
    CHECK(same);
    for (uint32_t j = 1; j < count; j++) CHECK(back[j - 1].tick <= back[j].tick);
  }
}

TEST(testSmfReadRunningStatusAndRescale)
{
  // Type 0, 480 PPQN, running status, a SysEx and a text meta event in between
  const uint8_t file[] = {'M', 'T', 'h', 'd', 0, 0, 0, 6, 0, 0, 0, 1, 0x01, 0xE0, 'M', 'T', 'r', 'k', 0, 0, 0, 25,
                          0x00, 0x90, 60, 100,       // note-on at 0
                          0x83, 0x60, 62, 100,       // running status, 480 ticks later (= 96)
                          0x00, 0xF0, 0x02, 0x7E, 0xF7, // SysEx, skipped
                          0x00, 0xFF, 0x01, 0x01, 'x',  // text, skipped
                          0x00, 0x80, 60, 0,         // note-off
                          0x00, 0xFF, 0x2F, 0x00};
  LoopEvent back[8];
  uint32_t count = 0;
  Smf::Info info;
  CHECK(Smf::read(file, sizeof(file), back, 8, count, info));
  CHECK(count == 3 && back[0].tick == 0 && back[1].tick == 96 && back[1].status == 0x90 && back[1].data1 == 62);
  CHECK(back[2].status == 0x80 && info.lengthTicks == 384);
  CHECK(!Smf::read((const uint8_t *)"nonsense", 8, back, 8, count, info));
}

struct SentLog
{
  std::vector<std::pair<uint64_t, std::vector<uint8_t>>> sent;
  static void listener(MidiOutput out, const MidiMessage &msg, Origin origin, void *ctx)
  {
    if (out != MidiOutput::Din || origin.kind != Origin::Internal || origin.owner != Looper::kOwner) return;
    static_cast<SentLog *>(ctx)->sent.push_back({gFakeMicros64, std::vector<uint8_t>(msg.bytes, msg.bytes + msg.length)});
  }
};

/*! Run clock and looper for `micros` (the looper's loop hook every millisecond) */
static void runLooper(OutputRig &rig, Looper &looper, uint64_t micros)
{
  uint64_t end = gFakeMicros64 + micros;
  for (; gFakeMicros64 < end; gFakeMicros64 += 100) {
    gFakeMicros = (uint32_t)gFakeMicros64;
    rig.clock.runDue(gFakeMicros64);
    if (gFakeMicros64 % 1000 == 0) looper.run((uint32_t)(gFakeMicros64 / 1000));
    rig.serial.drain(128);
    rig.din.pumpTx();
  }
}

static void play(Looper &looper, uint8_t status, uint8_t note, uint8_t velocity)
{
  uint8_t bytes[3] = {status, note, velocity};
  MidiMessage msg = {MidiSource::Usb, 0, bytes, 3, (uint32_t)gFakeMicros64};
  Looper::observer(msg, true, &looper);
}

TEST(testLooperRecordAndPlay)
{
  OutputRig rig;
  Looper looper;
  CHECK(looper.begin(rig.outputs, rig.clock, nullptr, 4096));
  SentLog log;
  rig.outputs.addListener(SentLog::listener, &log);
  looper.config().bars = 1;
  looper.changed();
  looper.rec();
  runLooper(rig, looper, 2000);
  CHECK(looper.snapshot().state == LoopState::Armed);

  // Start: the first tick (1 ms later) is bar 1, recording starts there
  rig.clock.start();
  runLooper(rig, looper, 1000);
  uint64_t t0 = gFakeMicros64;
  runLooper(rig, looper, 500);
  CHECK(looper.snapshot().state == LoopState::Recording);
  runLooper(rig, looper, 500000 - 500 - 300); // beat 2 (a few hundred us early)
  play(looper, 0x90, 60, 100);
  runLooper(rig, looper, 250000);
  play(looper, 0x80, 60, 0);
  // Not routed (an echo or a filtered message): not recorded
  uint8_t echo[3] = {0x90, 70, 1};
  Looper::observer({MidiSource::Din, 0, echo, 3, (uint32_t)gFakeMicros64}, false, &looper);
  runLooper(rig, looper, 1900000 - 750000);
  play(looper, 0x90, 50, 90); // held across the loop end
  CHECK(log.sent.empty()); // nothing is played back while the first pass records
  runLooper(rig, looper, 200000);

  LoopSnapshot snap = looper.snapshot();
  CHECK(snap.state == LoopState::Looping && snap.events == 4 && snap.length == 96);

  // Second cycle: the recorded note comes back at beat 2, within a fraction of a tick
  runLooper(rig, looper, 1800000);
  bool found = false;
  for (auto &s : log.sent) {
    if (s.second.size() == 3 && s.second[0] == 0x90 && s.second[1] == 60) {
      int64_t at = (int64_t)(s.first - t0) - 2000000 - 500000;
      found = at > -6000 && at < 6000;
    }
  }
  CHECK(found);
  // The held note was cut at the loop end: its note-off comes back at the end of the cycle
  runLooper(rig, looper, 150000);
  bool cut = false;
  for (auto &s : log.sent) cut |= s.second[0] == 0x80 && s.second[1] == 50;
  CHECK(cut);

  // Stop releases everything still sounding (the held note 50 replays from 1.9 s)
  runLooper(rig, looper, 1900000);
  CHECK(rig.tracker.isHeld(MidiOutput::Din, 0, 50)); // replayed at 1.9 s, still sounding
  rig.clock.stop();
  runLooper(rig, looper, 5000);
  CHECK(rig.tracker.heldCount(MidiOutput::Din) == 0);
  CHECK(!looper.snapshot().active);
}

TEST(testLooperOverdubAndUndo)
{
  OutputRig rig;
  Looper looper;
  looper.begin(rig.outputs, rig.clock, nullptr, 4096);
  looper.config().bars = 1;
  looper.changed();
  looper.rec();
  runLooper(rig, looper, 2000);
  rig.clock.start();
  runLooper(rig, looper, 300000);
  play(looper, 0x90, 60, 100);
  runLooper(rig, looper, 100000);
  play(looper, 0x80, 60, 0);
  runLooper(rig, looper, 1700000); // first pass ends
  CHECK(looper.snapshot().events == 2 && looper.snapshot().passes == 1);

  // Overdub a second pass: it joins at the next loop end
  looper.toggleOverdub();
  runLooper(rig, looper, 2000);
  CHECK(looper.snapshot().overdub);
  runLooper(rig, looper, 500000);
  play(looper, 0x91, 64, 80);
  runLooper(rig, looper, 100000);
  play(looper, 0x81, 64, 0);
  runLooper(rig, looper, 1500000);
  CHECK(looper.snapshot().events == 4 && looper.snapshot().passes == 2);
  looper.toggleOverdub();
  runLooper(rig, looper, 2000);
  CHECK(!looper.snapshot().overdub);

  looper.undo();
  runLooper(rig, looper, 2000);
  CHECK(looper.snapshot().events == 2 && looper.snapshot().passes == 1);
  looper.clear();
  runLooper(rig, looper, 2000);
  CHECK(looper.snapshot().state == LoopState::Empty && looper.snapshot().events == 0);

  // Export / import round trip through the transfer buffer
  looper.rec();
  runLooper(rig, looper, 2000);
  rig.clock.stop();
  runLooper(rig, looper, 2000);
  LoopEvent *t = looper.transferBuffer();
  t[0] = {0, 0x90, 40, 100, 0};
  t[1] = {48, 0x80, 40, 0, 0};
  looper.importTransfer(2, 768);
  runLooper(rig, looper, 2000);
  CHECK(looper.snapshot().state == LoopState::Looping && looper.snapshot().events == 2 && looper.snapshot().length == 192);
  looper.requestExport();
  runLooper(rig, looper, 2000);
  CHECK(looper.exportReady() && looper.transferCount() == 2 && looper.transferLength() == 768);
  CHECK(isOn(looper.transfer()[0]));
}
