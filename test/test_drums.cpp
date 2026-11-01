// Host tests for the drum machine (stage 5)
#include <Arduino.h> // fake millis()/micros(): gFakeMillis
#include "test_support.h"
#define private public
#include "drum_machine.h"
#undef private
#include "input_observers.h"

/*! Number of note-ons for `note` on channel 10 in a DIN byte stream */
static int noteOns(const std::vector<uint8_t> &bytes, uint8_t note)
{
  int n = 0;
  for (size_t i = 0; i + 2 < bytes.size(); i++) {
    if (bytes[i] == 0x99 && bytes[i + 1] == note && bytes[i + 2] > 0) n++;
  }
  return n;
}

struct DrumRig : OutputRig
{
  DrumMachine drums;
  DrumRig()
  {
    InputHooks::registry() = InputHooks::Registry();
    drums.begin(outputs, clock);
    gRunRig = this;
  }
  ~DrumRig()
  {
    InputHooks::registry() = InputHooks::Registry();
    gRunRig = nullptr;
  }
  /*! Run the clock (and drum machine) for `micros`, then everything sent to DIN */
  std::vector<uint8_t> play(uint64_t micros)
  {
    serial.out.clear();
    runFor(clock, micros);
    return dinBytes();
  }
};

TEST(testDrumPlayback)
{
  DrumRig rig;
  for (int s = 0; s < 16; s += 4) rig.drums.setStep(0, 0, s, DrumStep::On); // kick on the beats
  rig.drums.setStep(0, 1, 4, DrumStep::Accent);                             // accented snare on beat 2
  rig.clock.start();
  // 120 BPM: a 16th every 125 ms, so 8 steps (0-7) in the first second
  std::vector<uint8_t> out = rig.play(1000000);
  CHECK(noteOns(out, 36) == 2);
  CHECK(contains(out, {0x99, 38, 127})); // accent velocity
  CHECK(contains(out, {0x89, 36, 0}));   // note-off half a step later
  CHECK(rig.drums.playing() && rig.drums.position() == 7);

  // Mute and solo
  DrumLane kick = rig.drums.bank().lanes[0];
  kick.muted = true;
  rig.drums.setLane(0, kick);
  out = rig.play(1000000); // steps 8-15
  CHECK(noteOns(out, 36) == 0);
  kick.muted = false;
  rig.drums.setLane(0, kick);
  DrumLane snare = rig.drums.bank().lanes[1];
  snare.solo = true;
  rig.drums.setLane(1, snare);
  out = rig.play(2000000);
  CHECK(noteOns(out, 36) == 0 && noteOns(out, 38) == 1);

  // Stop: everything still sounding is released, nothing more plays
  snare.solo = false;
  rig.drums.setLane(1, snare);
  rig.drums.setStep(0, 2, 0, DrumStep::On);
  rig.clock.stop();
  rig.play(10000);
  CHECK(!rig.drums.playing() && rig.tracker.heldCount(MidiOutput::Din) == 0);
  out = rig.play(1000000);
  CHECK(noteOns(out, 36) == 0);
}

TEST(testDrumPatternSwitch)
{
  DrumRig rig;
  rig.drums.setLength(0, 4);
  rig.drums.setStep(0, 0, 0, DrumStep::On); // pattern 0: kick on step 1 of 4
  rig.drums.setStep(1, 1, 0, DrumStep::On); // pattern 1: snare on step 1 of 16
  rig.clock.start();
  rig.play(130000); // step 0
  rig.drums.queuePattern(1);
  CHECK(rig.drums.playingPattern() == 0 && rig.drums.queuedPattern() == 1);
  rig.play(300000); // steps 1-3: still pattern 0
  CHECK(rig.drums.playingPattern() == 0);
  std::vector<uint8_t> out = rig.play(125000); // the pattern ends: pattern 1 starts with its snare
  CHECK(rig.drums.playingPattern() == 1 && rig.drums.queuedPattern() == -1);
  CHECK(noteOns(out, 38) == 1 && noteOns(out, 36) == 0);

  // Chain 0, 1: alternates at each pattern end (pattern 1 is 16 steps)
  uint8_t chain[2] = {0, 1};
  rig.drums.setChain(chain, 2);
  rig.clock.stop();
  rig.clock.start();
  rig.play(130000);
  CHECK(rig.drums.playingPattern() == 0);
  rig.play(500000);
  CHECK(rig.drums.playingPattern() == 1);
  rig.drums.setChain(nullptr, 0);

  // Stopped: a pattern switch happens at once
  rig.clock.stop();
  rig.play(10000);
  rig.drums.queuePattern(5);
  CHECK(rig.drums.playingPattern() == 5 && rig.drums.queuedPattern() == -1);
}

TEST(testDrumPads)
{
  DrumRig rig;
  auto pad = [&](uint8_t status, uint8_t note, uint8_t velocity) {
    MidiMessage m = msg(MidiSource::Usb, {status, note, velocity});
    return DrumMachine::onInput(m, &rig.drums);
  };
  // Lane notes on the pad channel (10) play and are consumed; others pass through
  CHECK(pad(0x99, 38, 90));
  CHECK(rig.dinBytes() == std::vector<uint8_t>({0x99, 38, 90}));
  CHECK(pad(0x89, 38, 0));
  CHECK(rig.dinBytes() == std::vector<uint8_t>({0x89, 38, 0}));
  CHECK(!pad(0x99, 70, 90)); // not a lane note
  CHECK(!pad(0x90, 38, 90)); // channel 1
  rig.drums.setPadChannel(0);
  CHECK(!pad(0x99, 38, 90)); // pads off
  rig.drums.setPadChannel(10);

  // Recording: a hit right after a step lands on that step, a hard hit records an accent
  rig.drums.setRecording(true);
  rig.clock.start();
  rig.play(130000 + 125000 * 2); // steps 0-2
  int position = rig.drums.position();
  pad(0x99, 42, 120);
  CHECK(rig.drums.step(0, 2, position) == DrumStep::Accent);
  // Late in a step counts for the next one
  rig.play(100000);
  position = rig.drums.position();
  pad(0x99, 36, 80);
  CHECK(rig.drums.step(0, 0, (position + 1) % 16) == DrumStep::On);
}

TEST(testDrumPersistence)
{
  {
    DrumRig rig;
    rig.drums.setStep(3, 4, 31, DrumStep::Accent);
    rig.drums.setLength(3, 32);
    gFakeMillis += 3000;
    rig.drums.poll(); // stopped and idle for 2 s: saved
  }
  HardwareSerial serial;
  DinMidi din;
  UsbMidi usb;
  NoteTracker tracker;
  MidiOutputs outputs;
  ClockEngine clock;
  din.begin(serial, 0, 0, record);
  outputs.begin(din, usb, tracker);
  clock.begin(outputs);
  DrumMachine drums;
  drums.begin(outputs, clock);
  CHECK(drums.step(3, 4, 31) == DrumStep::Accent && drums.bank().patterns[3].length == 32);
  InputHooks::registry() = InputHooks::Registry();

  // Speaker hits: queued for the UI as they play
  uint8_t note, velocity;
  drums.audition(2);
  drums.run(0);
  CHECK(drums.takeHit(note, velocity) && note == 42 && velocity == 90);
  CHECK(!drums.takeHit(note, velocity));
}
