// Stage 15: setlist and scenes
#include "test_support.h"

#include <Arduino.h> // fake millis()
#include "input_observers.h"
#include "scenes.h"

namespace
{
/*! An output rig with the clock driven in 1 ms steps, like the MIDI task does */
struct SceneRig : OutputRig
{
  SamSynth synth;
  Scenes scenes;

  /*! `nvs`: stored settings to boot with (OutputRig clears the fake NVS) */
  explicit SceneRig(const decltype(gFakeNvs) *nvs = nullptr)
  {
    if (nvs) gFakeNvs = *nvs;
    InputHooks::registry() = InputHooks::Registry();
    gFakeMillis = 1000;
    synth.begin(outputs);
    SceneTargets targets;
    targets.clock = &clock;
    targets.router = &router;
    targets.synth = &synth;
    scenes.begin(outputs, targets);
  }
  ~SceneRig() { InputHooks::registry() = InputHooks::Registry(); }

  void step(uint64_t micros)
  {
    uint64_t end = gFakeMicros64 + micros;
    while (gFakeMicros64 < end) {
      gFakeMicros64 += 1000;
      gFakeMillis = (uint32_t)(gFakeMicros64 / 1000) + 1000;
      clock.runDue(gFakeMicros64);
      scenes.run(gFakeMillis);
      serial.drain(128);
      din.pumpTx();
    }
  }
  std::vector<uint8_t> taken()
  {
    std::vector<uint8_t> out = dinBytes();
    std::vector<uint8_t> kept;
    for (uint8_t b : out) {
      if (b < 0xF8) kept.push_back(b); // ignore clock bytes, should any go to DIN
    }
    return kept;
  }
  void setDevice(int d, uint8_t channel, bool pcAllowed, uint8_t lead = 0)
  {
    SceneDevice &device = scenes.device(d);
    device.channel = channel;
    device.pcAllowed = pcAllowed;
    device.leadBeats = lead;
  }
};
} // namespace

TEST(testScenesProgramGuard)
{
  SceneRig rig;
  rig.setDevice(0, 5, false);
  CHECK(!rig.scenes.setProgram(0, 0, 12)); // PCs not allowed for this device
  rig.scenes.scene(0).devices[0].program = 12; // even if one got in, it is never sent
  rig.scenes.recall(0, true);
  rig.scenes.run(gFakeMillis);
  CHECK(rig.taken().empty());
  CHECK(rig.scenes.status().fired == 1);
}

TEST(testScenesRecallNow)
{
  SceneRig rig;
  rig.setDevice(0, 5, true);
  CHECK(rig.scenes.setProgram(0, 0, 12));
  SceneDeviceMessages &messages = rig.scenes.scene(0).devices[0];
  messages.bankMsb = 1;
  for (int i = 0; i < 6; i++) messages.ccs[i] = {(uint8_t)(70 + i), (uint8_t)(10 + i)};
  messages.ccCount = 6;

  // Transport stopped: a quantised recall happens at once. Bank, PC, then the first CC chunk
  rig.scenes.recall(0);
  rig.scenes.run(gFakeMillis);
  std::vector<uint8_t> out = rig.taken();
  CHECK(out == std::vector<uint8_t>({0xB4, 0, 1, 0xC4, 12, 0xB4, 70, 10, 0xB4, 71, 11, 0xB4, 72, 12, 0xB4, 73, 13}));
  // The rest of the snapshot follows 10 ms later, not before
  rig.scenes.run(gFakeMillis + 5);
  CHECK(rig.taken().empty());
  rig.scenes.run(gFakeMillis + 11);
  CHECK(rig.taken() == std::vector<uint8_t>({0xB4, 74, 14, 0xB4, 75, 15}));
  CHECK(rig.scenes.armed() == 0); // only one scene: it stays armed
}

TEST(testScenesNextBarAndLead)
{
  SceneRig rig;
  rig.setDevice(0, 10, true, 1); // e.g. an Elektron: PC one beat early
  rig.setDevice(1, 2, true, 0);
  rig.scenes.setProgram(0, 0, 3);
  rig.scenes.setProgram(0, 1, 7);
  rig.clock.start();
  rig.step(500000); // 120 BPM: a beat is 0.5 s, a bar 2 s
  rig.taken();
  rig.scenes.recall(0); // timing: next bar (default)
  rig.step(900000);  // 1.4 s: still before the last beat of the bar
  CHECK(rig.taken().empty());
  SceneStatus status = rig.scenes.status();
  CHECK(status.pending && status.ticksLeft > 0 && status.fired == 0);
  rig.step(200000); // 1.6 s: inside the lead time
  CHECK(rig.taken() == std::vector<uint8_t>({0xC9, 3}));
  CHECK(rig.scenes.status().pending);
  rig.step(500000); // 2.1 s: the bar line has passed
  CHECK(rig.taken() == std::vector<uint8_t>({0xC1, 7}));
  status = rig.scenes.status();
  CHECK(!status.pending && status.fired == 1);
}

TEST(testScenesCancel)
{
  SceneRig rig;
  rig.setDevice(0, 4, true);
  rig.scenes.setProgram(0, 0, 9);
  rig.clock.start();
  rig.step(300000);
  rig.scenes.recall(0);
  rig.step(100000);
  rig.scenes.cancel(); // panic
  rig.step(3000000);
  CHECK(rig.taken().empty());
  CHECK(!rig.scenes.status().pending && rig.scenes.status().fired == 0);
}

TEST(testScenesCaptureAndApply)
{
  SceneRig rig;
  rig.router.config().forceChannel[0] = 5;
  rig.router.changed();
  rig.synth.config().listen = SynthListen::Layers;
  rig.synth.changed();
  rig.clock.config().bpm = 128;
  rig.clock.changed();
  CHECK(rig.scenes.capture(0));
  CHECK(rig.scenes.scene(0).snapshot == 0 && rig.scenes.scene(0).tempo == 128.0f);

  // Settings change, then the scene brings them back; display and output choices stay
  rig.router.config().forceChannel[0] = 9;
  rig.router.config().ledLevel = 3;
  rig.router.changed();
  rig.synth.config().listen = SynthListen::All;
  rig.synth.config().speaker = true;
  rig.synth.changed();
  rig.clock.config().bpm = 90;
  rig.clock.changed();
  rig.scenes.recall(0, true);
  rig.scenes.run(gFakeMillis);
  rig.scenes.poll(gFakeMillis);
  CHECK(rig.router.config().forceChannel[0] == 5 && rig.router.config().ledLevel == 3);
  CHECK(rig.synth.config().listen == SynthListen::Layers && rig.synth.config().speaker);
  CHECK(rig.clock.config().bpm == 128.0f);
  CHECK(rig.scenes.current() == 0);

  // The router's release-before-change applies: a held note ends on its old channel
  rig.router.config().forceChannel[0] = 9;
  rig.router.changed();
  rig.router.applyPending();
  rig.router.route(msg(MidiSource::Usb, {0x90, 60, 100})); // held, forced to channel 9
  rig.taken();
  rig.scenes.recall(0, true);
  rig.scenes.run(gFakeMillis);
  rig.scenes.poll(gFakeMillis);
  rig.router.applyPending();
  CHECK(contains(rig.taken(), {0x88, 60, 0}));

  // Clearing frees the snapshot slot
  rig.scenes.clearCapture(0);
  CHECK(rig.scenes.scene(0).snapshot == -1 && !rig.scenes.snapshot(0).used);
}

TEST(testScenesLearnCcs)
{
  SceneRig rig;
  rig.setDevice(1, 3, false);
  rig.scenes.setLearn(0);
  uint8_t cc[3] = {0xB2, 74, 99};
  InputHooks::observe({MidiSource::Usb, 0, cc, 3}, true);
  cc[2] = 100;
  InputHooks::observe({MidiSource::Usb, 0, cc, 3}, true); // same CC: updated, not added
  uint8_t other[3] = {0xB5, 1, 1};                        // no device on channel 6
  InputHooks::observe({MidiSource::Usb, 0, other, 3}, true);
  rig.scenes.poll(gFakeMillis);
  const SceneDeviceMessages &messages = rig.scenes.scene(0).devices[1];
  CHECK(messages.ccCount == 1 && messages.ccs[0].cc == 74 && messages.ccs[0].value == 100);
  rig.scenes.setLearn(-1);
  InputHooks::observe({MidiSource::Usb, 0, cc, 3}, true);
  rig.scenes.poll(gFakeMillis);
  CHECK(rig.scenes.scene(0).devices[1].ccCount == 1);
}

TEST(testScenesListAndPersistence)
{
  decltype(gFakeNvs) stored;
  {
    SceneRig rig;
    CHECK(rig.scenes.count() == 1);
    CHECK(rig.scenes.addScene(0) == 1 && rig.scenes.addScene(1) == 2);
    rig.scenes.scene(1).name = 4;
    rig.scenes.scene(2).timing = SceneTiming::NextPhrase;
    rig.setDevice(0, 10, true, 1);
    rig.scenes.setProgram(2, 0, 5);
    rig.scenes.changed(-1);
    rig.scenes.recall(1);
    CHECK(rig.scenes.armed() == 2);
    rig.scenes.poll(gFakeMillis + 3000); // stopped and idle: saved
    stored = gFakeNvs;
  }
  SceneRig loaded(&stored); // a fresh boot reads it back
  CHECK(loaded.scenes.count() == 3 && loaded.scenes.scene(1).name == 4);
  CHECK(loaded.scenes.scene(2).timing == SceneTiming::NextPhrase && loaded.scenes.scene(2).devices[0].program == 5);
  CHECK(loaded.scenes.device(0).channel == 10 && loaded.scenes.device(0).pcAllowed);
  loaded.scenes.removeScene(1);
  CHECK(loaded.scenes.count() == 2 && loaded.scenes.scene(1).timing == SceneTiming::NextPhrase);
  loaded.scenes.removeScene(0);
  loaded.scenes.removeScene(0);
  CHECK(loaded.scenes.count() == 1); // the last scene is kept (cleared)
  gFakeNvs.clear();
}
