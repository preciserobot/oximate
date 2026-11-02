// Tests for stage 10: haptic cue player, clock listener, kick observer
#include <Arduino.h>
#include "test_support.h"
#include "haptic_patterns.h"
#include "haptics.h"

namespace
{
struct FakeMotor
{
  std::vector<uint8_t> levels;
  std::vector<bool> clicks;
  static void vibrate(uint8_t level, void *ctx) { static_cast<FakeMotor *>(ctx)->levels.push_back(level); }
  static void click(bool accent, void *ctx) { static_cast<FakeMotor *>(ctx)->clicks.push_back(accent); }
  Haptics::Output output() { return {vibrate, click, this}; }
};

void drainCues()
{
  Cues::Item item;
  while (Cues::queue().take(item)) {
  }
}
} // namespace

TEST(testHapticPatterns)
{
  drainCues();
  gFakeNvs.clear();
  FakeMotor motor;
  Haptics haptics;
  haptics.begin(motor.output(), nullptr);
  haptics.config().enabled = true; // cues are off by default
  haptics.changed();

  // A beat: full level for 70 ms, then off (each level written once)
  Cues::post(Cue::Beat);
  haptics.service(1000000);
  CHECK(motor.levels == std::vector<uint8_t>({235}));
  haptics.service(1000000 + 69000);
  CHECK(motor.levels.size() == 1);
  haptics.service(1000000 + 70000);
  CHECK(motor.levels == std::vector<uint8_t>({235, 0}) && !haptics.playing());

  // Strength scales every level
  motor.levels.clear();
  haptics.config().strength = 120;
  Cues::post(Cue::PhraseStart);
  haptics.service(2000000);
  haptics.service(2140000);
  haptics.service(2230000);
  haptics.service(2370000);
  CHECK(motor.levels == std::vector<uint8_t>({120, 0, 120, 0}));
  haptics.config().strength = 235;

  // Priorities: a downbeat interrupts a touch tick; a touch tick doesn't interrupt a downbeat
  haptics.config().outputs[(int)CueGroup::Touch] = CueOutput::Vibrate;
  Cues::post(Cue::TouchTick);
  haptics.service(3000000);
  Cues::post(Cue::Downbeat);
  haptics.service(3005000);
  CHECK(haptics.playing() && haptics.current() == Cue::Downbeat);
  Cues::post(Cue::TouchTick);
  haptics.service(3010000);
  CHECK(haptics.current() == Cue::Downbeat);
  haptics.service(3200000);

  // Groups switched off are silent; the kick is off by default
  motor.levels.clear();
  Cues::post(Cue::Kick, 0, 127);
  haptics.service(4000000);
  CHECK(motor.levels.empty());
  // Kick length follows velocity: 40 ms soft .. 90 ms hard
  CHECK(HapticPatterns::kickMillis(0) == 40 && HapticPatterns::kickMillis(127) == 90);
  haptics.config().outputs[(int)CueGroup::Kick] = CueOutput::Vibrate;
  haptics.changed();
  Cues::post(Cue::Kick, 0, 127);
  haptics.service(5000000);
  haptics.service(5089000);
  CHECK(haptics.playing());
  haptics.service(5090000);
  CHECK(!haptics.playing());

  // Click output: no motor, an accented click on the downbeat
  motor.levels.clear();
  haptics.config().outputs[(int)CueGroup::Metronome] = CueOutput::Click;
  Cues::post(Cue::Downbeat);
  Cues::post(Cue::Beat);
  haptics.service(6000000);
  CHECK(motor.levels.empty() && motor.clicks == std::vector<bool>({true, false}));

  // Test sequence plays every pattern, also those switched off
  haptics.config().outputs[(int)CueGroup::Metronome] = CueOutput::Off;
  motor.levels.clear();
  haptics.test();
  for (uint32_t t = 7000000; t < 7000000 + 12 * 700000; t += 1000) haptics.service(t);
  int starts = 0;
  for (uint8_t level : motor.levels) starts += level > 0 && level != 0;
  CHECK(starts >= 12);
  drainCues();
}

TEST(testHapticScheduling)
{
  drainCues();
  gFakeNvs.clear();
  OutputRig rig;
  FakeMotor motor;
  Haptics haptics;
  haptics.begin(motor.output(), &rig.clock);
  haptics.config().enabled = true; // cues are off by default
  haptics.changed();

  // A scheduled beat waits for its time, and is dropped when the clock isn't running then
  Cues::post(Cue::Beat, 1050000);
  haptics.service(1000000);
  CHECK(motor.levels.empty());
  haptics.service(1050000);
  CHECK(motor.levels.empty());

  // The clock listener: beat 1 now, then each next beat one period ahead minus the lead
  rig.clock.addListener(Haptics::clockListener, &haptics);
  gFakeMicros64 = 0;
  rig.clock.start();
  rig.clock.runDue(0);
  for (gFakeMicros64 = 0; gFakeMicros64 < 30000; gFakeMicros64 += 100) {
    gFakeMicros = (uint32_t)gFakeMicros64;
    rig.clock.runDue(gFakeMicros64);
  }
  Cues::Item first, next;
  CHECK(Cues::queue().take(first) && first.cue == Cue::Downbeat && first.dueMicros == 0);
  CHECK(Cues::queue().take(next) && next.cue == Cue::Beat);
  uint32_t expected = gFakeMicros + 500000 - 25000; // 120 BPM, 25 ms lead
  CHECK(next.dueMicros > expected - 30000 && next.dueMicros <= expected);

  // Running now: the scheduled beat plays when due
  Cues::post(next.cue, next.dueMicros);
  haptics.service(next.dueMicros - 1000);
  CHECK(motor.levels.empty());
  haptics.service(next.dueMicros);
  CHECK(motor.levels == std::vector<uint8_t>({235}));
  drainCues();
}

TEST(testHapticClockAlertsAndKick)
{
  drainCues();
  gFakeNvs.clear();
  OutputRig rig;
  FakeMotor motor;
  Haptics haptics;
  haptics.begin(motor.output(), &rig.clock);
  haptics.config().enabled = true; // cues are off by default
  haptics.changed();

  // Following DIN: lock, lose, lock again -> ClockLost, then ClockLocked
  rig.clock.config().mode = ClockMode::FollowDin;
  rig.clock.changed();
  rig.clock.runDue(0);
  uint8_t tick = 0xF8;
  auto ticks = [&](int n) {
    for (int i = 0; i < n; i++) {
      gFakeMicros64 += 20833;
      rig.router.route({MidiSource::Din, 0, &tick, 1, (uint32_t)gFakeMicros64});
      rig.clock.runDue(gFakeMicros64);
    }
  };
  ticks(30);
  haptics.service(1000000);
  gFakeMicros64 += 600000;
  rig.clock.runDue(gFakeMicros64);
  haptics.service(2000000); // notices the loss, posts it and plays it
  CHECK(haptics.playing() && haptics.current() == Cue::ClockLost);
  haptics.service(2500000);
  ticks(30);
  haptics.service(3000000);
  CHECK(haptics.playing() && haptics.current() == Cue::ClockLocked);
  haptics.service(3500000);
  Cues::Item item;

  // Kick observer: only when the kick group is on, only the configured note and channel
  uint8_t kick[3] = {0x99, 36, 100}, snare[3] = {0x99, 38, 100};
  Haptics::kickObserver({MidiSource::Din, 0, kick, 3}, true, &haptics);
  CHECK(!Cues::queue().take(item));
  haptics.config().outputs[(int)CueGroup::Kick] = CueOutput::Vibrate;
  haptics.changed();
  Haptics::kickObserver({MidiSource::Din, 0, snare, 3}, true, &haptics);
  Haptics::kickObserver({MidiSource::Din, 0, kick, 3}, true, &haptics);
  CHECK(Cues::queue().take(item) && item.cue == Cue::Kick && item.velocity == 100);
  CHECK(!Cues::queue().take(item));

  // Settings survive a save and load
  haptics.config().strength = 150;
  haptics.changed();
  gFakeMillis += 3000;
  haptics.poll();
  Haptics reloaded;
  reloaded.begin(motor.output(), nullptr);
  CHECK(reloaded.config().strength == 150 && reloaded.config().outputs[(int)CueGroup::Kick] == CueOutput::Vibrate);
  drainCues();
}

TEST(testHapticsOffByDefault)
{
  gFakeNvs.clear();
  FakeMotor motor;
  Haptics haptics;
  haptics.begin(motor.output(), nullptr);
  CHECK(!haptics.config().enabled);
  Cues::post(Cue::Beat);
  Cues::post(Cue::PanicDone);
  haptics.service(1000);
  CHECK(motor.levels.empty() && !haptics.playing());
  // Switched on: cues play; switched off again: the motor stops at once
  haptics.config().enabled = true;
  haptics.changed();
  Cues::post(Cue::ClockLost);
  haptics.service(2000);
  CHECK(!motor.levels.empty() && motor.levels.back() > 0);
  haptics.config().enabled = false;
  haptics.changed();
  CHECK(motor.levels.back() == 0 && !haptics.playing());
}
