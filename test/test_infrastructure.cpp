// Tests for shared infrastructure: input hooks and the UI -> MIDI task send queue
#include "test_support.h"
#include "input_observers.h"

TEST(testSendQueue)
{
  OutputRig rig;
  static SendQueue queue;
  uint8_t note[3] = {0x95, 60, 90};
  CHECK(queue.push(MidiOutput::Din, note, 3, Origin::internal(7)));
  CHECK(rig.dinBytes().empty()); // nothing until the MIDI task drains it
  queue.drain(rig.outputs);
  CHECK(rig.dinBytes() == std::vector<uint8_t>({0x95, 60, 90}));
  CHECK(rig.tracker.isHeld(MidiOutput::Din, 5, 60));
  int pushed = 0;
  while (queue.push(MidiOutput::Din, note, 3, Origin::internal(7))) pushed++;
  CHECK(pushed == SendQueue::kLength - 1);
}

static int gObserved = 0;
static bool consumeChannel16(const MidiMessage &msg, void *) { return msg.isChannel() && msg.channel() == 16; }
static void countObserved(const MidiMessage &, bool forwarded, void *) { gObserved += forwarded ? 10 : 1; }

TEST(testInputHooks)
{
  InputHooks::registry() = InputHooks::Registry();
  InputHooks::addInterceptor(consumeChannel16, nullptr);
  InputHooks::addObserver(countObserved, nullptr);
  uint8_t a[3] = {0x9F, 60, 1}, b[3] = {0x90, 60, 1};
  CHECK(InputHooks::intercept({MidiSource::Usb, 0, a, 3}));
  CHECK(!InputHooks::intercept({MidiSource::Usb, 0, b, 3}));
  InputHooks::observe({MidiSource::Usb, 0, b, 3}, true);
  InputHooks::observe({MidiSource::Usb, 0, a, 3}, false);
  CHECK(gObserved == 11);
  InputHooks::registry() = InputHooks::Registry();
}

#include "cue_queue.h"

TEST(testCueQueue)
{
  static Cues::Queue queue;
  CHECK(queue.post(Cue::Beat, 1000));
  CHECK(queue.post(Cue::Kick, 0, 100));
  Cues::Item item;
  CHECK(queue.take(item) && item.cue == Cue::Beat && item.dueMicros == 1000);
  CHECK(queue.take(item) && item.cue == Cue::Kick && item.velocity == 100);
  CHECK(!queue.take(item));
  for (int i = 0; i < Cues::Queue::kLength + 3; i++) queue.post(Cue::TouchTick);
  CHECK(queue.dropped() == 4); // one slot always stays free
}

#include "local_sound.h"

TEST(testLocalSoundTap)
{
  OutputRig rig;
  static LocalSoundTap tap;
  tap.begin(rig.outputs);
  uint8_t on[3] = {0x92, 60, 100}, off[3] = {0x82, 60, 0}, cc[3] = {0xB2, 74, 1};
  rig.outputs.send(MidiOutput::Din, {MidiSource::Din, 0, on, 3}, Origin::internal(12)); // Gen
  rig.outputs.send(MidiOutput::Din, {MidiSource::Din, 0, cc, 3}, Origin::internal(12)); // not a note
  rig.outputs.send(MidiOutput::Din, {MidiSource::Din, 0, on, 3}, Origin::internal(5));  // drums: own path
  rig.outputs.send(MidiOutput::Din, {MidiSource::Din, 0, on, 3}, Origin::fromInput(MidiSource::Usb)); // input
  rig.outputs.send(MidiOutput::Din, {MidiSource::Din, 0, off, 3}, Origin::internal(12));
  LocalSoundTap::Note note;
  CHECK(tap.take(note) && note.channel == 3 && note.note == 60 && note.velocity == 100);
  CHECK(tap.take(note) && note.velocity == 0);
  CHECK(!tap.take(note));
}
