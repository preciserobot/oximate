// Host tests for the generative sequencers (stage 12)
#include <Arduino.h> // fake millis()/micros()
#include "test_support.h"
#include "euclid.h"
#include "gen_core.h"
#define private public
#include "drum_machine.h"
#include "gen_engine.h"
#undef private
#include "input_observers.h"

static std::string pattern(uint32_t mask, int steps)
{
  std::string s;
  for (int i = 0; i < steps; i++) s += (mask & (1u << i)) ? 'x' : '.';
  return s;
}

TEST(testEuclid)
{
  CHECK(pattern(euclid(8, 3, 0), 8) == "x..x..x.");  // tresillo
  bool cinquillo = false; // x.xx.xx., up to rotation
  for (int r = 0; r < 8; r++) cinquillo = cinquillo || pattern(euclid(8, 5, r), 8) == "x.xx.xx.";
  CHECK(cinquillo);
  CHECK(pattern(euclid(16, 4, 0), 16) == "x...x...x...x..."); // four on the floor
  CHECK(pattern(euclid(8, 3, 1), 8) == ".x..x..x");  // rotation moves it later
  CHECK(pattern(euclid(8, 3, 9), 8) == ".x..x..x");  // rotation wraps
  CHECK(euclid(8, 0, 0) == 0 && euclid(8, 8, 0) == 0xFF && euclid(8, 12, 0) == 0xFF);
  CHECK(__builtin_popcount(euclid(13, 5, 3)) == 5 && euclid(13, 5, 3) < (1u << 13));
  CHECK(euclid(32, 32, 0) == 0xFFFFFFFFu);
}

TEST(testScalesAndQuantise)
{
  using namespace MusicTheory;
  uint16_t minor = kScales[ScaleMinor].mask;
  CHECK(quantise(61, 0, minor) == 60);  // C# -> C (tie goes down: C and D are both 1 away)
  CHECK(quantise(64, 0, minor) == 63);  // E -> Eb
  CHECK(quantise(66, 0, minor) == 65);  // F# -> F (tie down)
  CHECK(quantise(69, 9, minor) == 69);  // A in A minor
  CHECK(quantise(61, 9, minor) == 60);  // C# in A minor -> C
  for (int note = 0; note < 128; note++) {
    int q = quantise(note, 2, kScales[ScaleDorian].mask);
    CHECK(kScales[ScaleDorian].mask & (1 << ((q - 2 + 120) % 12)));
    CHECK(q - note <= 2 && note - q <= 2);
  }
  CHECK(quantise(60, 0, kScales[ScaleChromatic].mask) == 60);
}

TEST(testTuringRegister)
{
  TuringRegister reg;
  reg.reseed(1234);
  reg.setLength(5);
  std::vector<uint8_t> first, second;
  for (int i = 0; i < 5; i++) first.push_back(reg.step(0));
  for (int i = 0; i < 5; i++) second.push_back(reg.step(0));
  CHECK(first == second); // locked: repeats exactly every length steps

  TuringRegister a, b;
  a.reseed(99);
  b.reseed(99);
  a.setLength(8);
  b.setLength(8);
  bool same = true;
  for (int i = 0; i < 64; i++) same = same && a.step(30) == b.step(30);
  CHECK(same); // the same seed mutates the same way

  TuringRegister c;
  c.reseed(7);
  c.setLength(16);
  std::vector<uint8_t> loop1, loop2;
  for (int i = 0; i < 16; i++) loop1.push_back(c.step(100));
  for (int i = 0; i < 16; i++) loop2.push_back(c.step(100));
  int changed = 0;
  for (int i = 0; i < 16; i++) changed += loop1[i] != loop2[i];
  CHECK(changed >= 12); // at 100 % nearly every value changes each loop

  // Notes: always in the scale and the range; density 0 = silence, 100 = no rests
  uint16_t phrygian = MusicTheory::kScales[MusicTheory::ScalePhrygian].mask;
  for (int v = 0; v < 256; v++) {
    int note = turingNote(v, 100, 4, phrygian, 3, 2);
    CHECK(note >= 48 && note <= 73);
    CHECK(phrygian & (1 << ((note - 4 + 120) % 12)));
    CHECK(turingNote(v, 0, 4, phrygian, 3, 2) == -1);
  }
}

TEST(testModShapes)
{
  CHECK(modValue(ModShape::RampUp, 0, 0, 0, 0, 127) == 0);
  CHECK(modValue(ModShape::RampUp, 1, 0, 0, 0, 127) == 127);
  CHECK(modValue(ModShape::RampDown, 0, 0, 0, 0, 127) == 127);
  CHECK(modValue(ModShape::Sine, 0.5f, 0, 0, 20, 100) == 100);
  CHECK(modValue(ModShape::Sine, 0, 0, 0, 20, 100) == 20);
  CHECK(modValue(ModShape::Triangle, 0.5f, 0, 0, 0, 100) == 100);
  CHECK(modValue(ModShape::Square, 0.25f, 0, 0, 0, 100) == 100 && modValue(ModShape::Square, 0.75f, 0, 0, 0, 100) == 0);
  CHECK(modValue(ModShape::SampleHold, 0.9f, 0.5f, 1, 0, 100) == 50);
  CHECK(modValue(ModShape::SmoothRandom, 0, 0.2f, 0.8f, 0, 100) == 20);
  CHECK(modValue(ModShape::SmoothRandom, 1, 0.2f, 0.8f, 0, 100) == 80);
  CHECK(modValue(ModShape::RampUp, 1, 0, 0, 100, 20) == 20); // max < min inverts
}

/*! Note-ons for `note` on `channel` (1-16) in a DIN byte stream */
static int noteOnsOn(const std::vector<uint8_t> &bytes, uint8_t channel, int note = -1)
{
  int n = 0;
  for (size_t i = 0; i + 2 < bytes.size(); i++) {
    if (bytes[i] == (0x90 | (channel - 1)) && bytes[i + 2] > 0 && (note < 0 || bytes[i + 1] == note)) n++;
  }
  return n;
}

struct GenRig : OutputRig
{
  GenEngine gen;
  GenRig()
  {
    gen.begin(outputs, clock);
    gRunRig = this;
  }
  ~GenRig() { gRunRig = nullptr; }
  void apply()
  {
    gen.changed();
    gen.run(0);
  }
  std::vector<uint8_t> play(uint64_t micros)
  {
    serial.out.clear();
    runFor(clock, micros);
    return dinBytes();
  }
};

TEST(testGenEuclid)
{
  GenRig rig;
  GenConfig &c = rig.gen.config();
  for (GenSlot &s : c.slots) s.kind = GenKind::Off;
  c.slots[0].kind = GenKind::Euclid;
  c.slots[0].muted = false;
  c.slots[0].steps = 8;
  c.slots[0].hits = 3; // x..x..x.
  c.slots[0].note = 36;
  rig.apply();
  CHECK(rig.play(1000000).empty()); // nothing while the clock is stopped

  rig.clock.start();
  // 120 BPM: 8 sixteenths per second -> one cycle of E(3,8)
  std::vector<uint8_t> out = rig.play(1000000);
  CHECK(noteOnsOn(out, 10, 36) == 3);
  CHECK(contains(out, {0x89, 36, 0})); // gated note-off

  // Accents add velocity on their own Euclid
  c.slots[0].accentHits = 1;
  rig.apply();
  out = rig.play(1000000);
  CHECK(contains(out, {0x99, 36, 127}));

  // Probability 0 and mute send nothing
  c.slots[0].probability = 0;
  rig.apply();
  CHECK(noteOnsOn(rig.play(1000000), 10) == 0);
  c.slots[0].probability = 100;
  c.slots[0].muted = true;
  rig.apply();
  CHECK(noteOnsOn(rig.play(1000000), 10) == 0);

  // Polymeter: 3 over 8 against 2 over 3 line up again after lcm(8, 3) = 24 steps
  c.slots[0].muted = false;
  c.slots[1] = GenSlot();
  c.slots[1].kind = GenKind::Euclid;
  c.slots[1].muted = false;
  c.slots[1].steps = 3;
  c.slots[1].hits = 2;
  c.slots[1].note = 42;
  rig.apply();
  rig.clock.stop();
  rig.play(10000);
  CHECK(rig.tracker.heldCount(MidiOutput::Din) == 0); // stop releases
  rig.clock.start();
  out = rig.play(3000000); // 24 steps
  CHECK(noteOnsOn(out, 10, 36) == 9 && noteOnsOn(out, 10, 42) == 16);
  CHECK(rig.gen.status(0).position == 24 && rig.gen.status(1).position == 24);

  // 1/8 rate steps half as often
  c.slots[1].kind = GenKind::Off;
  c.slots[0].rate = GenRate::R8;
  rig.apply();
  rig.clock.stop();
  rig.clock.start();
  out = rig.play(2000000); // 16 sixteenths = 8 eighths = one cycle
  CHECK(noteOnsOn(out, 10, 36) == 3);
}

TEST(testGenTuringAndMod)
{
  GenRig rig;
  GenConfig &c = rig.gen.config();
  for (GenSlot &s : c.slots) s.kind = GenKind::Off;
  c.slots[2].kind = GenKind::Turing;
  c.slots[2].muted = false;
  c.slots[2].locked = true;
  c.slots[2].length = 4;
  c.slots[2].density = 100;
  c.slots[2].channel = 3;
  rig.apply();
  rig.clock.start();
  std::vector<uint8_t> out = rig.play(1000000); // 8 steps = two loops of 4
  std::vector<int> notes;
  for (size_t i = 0; i + 2 < out.size(); i++) {
    if (out[i] == 0x92 && out[i + 2] > 0) notes.push_back(out[i + 1]);
  }
  CHECK(notes.size() == 8);
  CHECK(notes.size() == 8 && std::equal(notes.begin(), notes.begin() + 4, notes.begin() + 4)); // locked repeats

  // Reseed (a new seed from the page) changes the loop
  c.slots[2].seed = 4242;
  rig.apply();
  out = rig.play(500000);
  std::vector<int> after;
  for (size_t i = 0; i + 2 < out.size(); i++) {
    if (out[i] == 0x92 && out[i + 2] > 0) after.push_back(out[i + 1]);
  }
  CHECK(after.size() == 4 && after != std::vector<int>(notes.begin(), notes.begin() + 4));

  // Mod lane: a ramp over one step sends CCs, only when the value changes
  c.slots[2].kind = GenKind::Off;
  c.slots[3].kind = GenKind::Mod;
  c.slots[3].muted = false;
  c.slots[3].shape = ModShape::RampUp;
  c.slots[3].period = 16;
  c.slots[3].channel = 3;
  c.slots[3].cc = 74;
  rig.apply();
  rig.clock.stop();
  rig.clock.start();
  out = rig.play(2000000); // one cycle (16 steps = 96 ticks)
  int ccs = 0, last = -1;
  bool rising = true;
  for (size_t i = 0; i + 2 < out.size(); i++) {
    if (out[i] == 0xB2 && out[i + 1] == 74) {
      if (out[i + 2] <= last) rising = false;
      last = out[i + 2];
      ccs++;
    }
  }
  CHECK(ccs > 60 && ccs <= 96 && rising);
}

TEST(testDrumEuclidFill)
{
  OutputRig rig;
  InputHooks::registry() = InputHooks::Registry();
  DrumMachine drums;
  drums.begin(rig.outputs, rig.clock);
  drums.setStep(0, 0, 0, DrumStep::Accent);
  drums.setStep(0, 0, 1, DrumStep::Accent);
  drums.fillEuclid(0, 0, 4);
  CHECK(drums.bank().patterns[0].on[0] == euclid(16, 4, 0));
  CHECK(drums.step(0, 0, 0) == DrumStep::Accent); // accent kept where it lands on a hit
  CHECK(drums.step(0, 0, 1) == DrumStep::Off);
  InputHooks::registry() = InputHooks::Registry();
}
