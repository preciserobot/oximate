// Stage 11: MIDI effects
#include "test_support.h"

#include "fx_engine.h"
#include "music_theory.h"

namespace
{
/*! An output rig with the effects engine wired into its router */
struct FxRig : OutputRig
{
  FxEngine fx;
  FxRig()
  {
    fx.begin(clock);
    router.setFx(&fx);
  }
  FxChainConfig &usb() { return fx.config().chains[(int)MidiSource::Usb]; }
  void apply()
  {
    fx.changed();
    fx.run();
  }
  bool play(std::initializer_list<uint8_t> bytes) { return router.route(msg(MidiSource::Usb, bytes)); }
  /*! Run the clock and the effects every 100 us for `micros`, pumping DIN */
  void runFor(uint64_t micros)
  {
    uint64_t end = gFakeMicros64 + micros;
    for (; gFakeMicros64 < end; gFakeMicros64 += 100) {
      clock.runDue(gFakeMicros64);
      fx.run();
      serial.drain(128);
      din.pumpTx();
    }
  }
};

std::vector<uint8_t> noteOns(const std::vector<uint8_t> &bytes)
{
  std::vector<uint8_t> notes;
  for (size_t i = 0; i + 2 < bytes.size(); i++) {
    if ((bytes[i] & 0xF0) == 0x90 && bytes[i + 2] > 0) {
      notes.push_back(bytes[i + 1]);
      i += 2;
    } else if (bytes[i] >= 0x80 && bytes[i] < 0xF0) {
      i += 2; // skip other 3-byte channel messages
    }
  }
  return notes;
}

void enable(FxChainConfig &c, FxSlot slot) { c.enabled |= 1 << (int)slot; }
} // namespace

TEST(testFxOffIsByteIdentical)
{
  // With every effect off, the router sends exactly what it sent before stage 11
  std::vector<std::vector<uint8_t>> stream = {
      {0x90, 60, 100}, {0xB0, 74, 33}, {0xC2, 5}, {0xE0, 0, 64}, {0x80, 60, 0},
      {0xF0, 0x7E, 0x01, 0xF7}, {0x95, 61, 1}, {0xA5, 61, 9}, {0x85, 61, 0},
  };
  for (int forced = 0; forced < 2; forced++) {
    OutputRig plain;
    FxRig withFx;
    plain.router.config().forceChannel[0] = forced ? 7 : 0;
    plain.router.changed();
    withFx.router.config().forceChannel[0] = forced ? 7 : 0;
    withFx.router.changed();
    for (auto &bytes : stream) {
      MidiMessage m = {MidiSource::Usb, 0, bytes.data(), (uint16_t)bytes.size()};
      CHECK(plain.router.route(m) == withFx.router.route(m));
    }
    CHECK(plain.dinBytes() == withFx.dinBytes());
  }
}

TEST(testFxTransposeAndNoteOff)
{
  FxRig rig;
  enable(rig.usb(), FxSlot::Transpose);
  rig.usb().transpose = 12;
  rig.apply();
  rig.play({0x90, 60, 100});
  CHECK(rig.dinBytes() == std::vector<uint8_t>({0x90, 72, 100}));
  // The setting changes while the key is held: its note-off still ends note 72
  rig.usb().transpose = 5;
  rig.apply();
  rig.play({0x80, 60, 0});
  CHECK(rig.dinBytes() == std::vector<uint8_t>({0x80, 72, 0}));
  CHECK(rig.tracker.heldCount(MidiOutput::Din) == 0);
  // Turning the slot off while a note is held: the note-off still follows the old mapping
  rig.play({0x90, 62, 90});
  rig.dinBytes();
  rig.usb().enabled = 0;
  rig.apply();
  rig.play({0x80, 62, 0});
  CHECK(rig.dinBytes() == std::vector<uint8_t>({0x80, 67, 0}));
  // Range edges clamp instead of wrapping
  CHECK(FxEngine::quantiseNote(127, 0, 0, FxScaleDir::Nearest) == 127);
  rig.usb().enabled = 1;
  rig.usb().transpose = 24;
  rig.apply();
  rig.play({0x90, 120, 1});
  CHECK(rig.dinBytes() == std::vector<uint8_t>({0x90, 127, 1}));
}

TEST(testFxScale)
{
  using namespace MusicTheory;
  uint8_t a = 9; // A minor
  CHECK(FxEngine::quantiseNote(61, a, ScaleMinor, FxScaleDir::Nearest) == 60); // C#: tie goes down to C
  CHECK(FxEngine::quantiseNote(61, a, ScaleMinor, FxScaleDir::Up) == 62);
  CHECK(FxEngine::quantiseNote(61, a, ScaleMinor, FxScaleDir::Down) == 60);
  CHECK(FxEngine::quantiseNote(69, a, ScaleMinor, FxScaleDir::Up) == 69);         // already in the scale
  CHECK(FxEngine::quantiseNote(61, a, ScaleChromatic, FxScaleDir::Nearest) == 61); // no scale: unchanged
  // Every scale and direction gives a note that is in the scale
  for (int scale = 1; scale < kScaleCount; scale++) {
    for (int dir = 0; dir < 3; dir++) {
      for (int note = 0; note < 128; note++) {
        uint8_t q = FxEngine::quantiseNote(note, 2, scale, (FxScaleDir)dir);
        CHECK((kScales[scale].mask >> ((q - 2 + 120) % 12)) & 1);
      }
    }
  }
}

TEST(testFxChord)
{
  FxRig rig;
  FxChainConfig &c = rig.usb();
  enable(c, FxSlot::Chord);
  rig.apply();
  rig.play({0x90, 60, 100});
  CHECK(rig.dinBytes() == std::vector<uint8_t>({0x90, 60, 100, 0x90, 64, 100, 0x90, 67, 100}));
  c.chordInversion = 1; // changed while held: the release still ends the root-position notes
  rig.apply();
  rig.play({0x80, 60, 0});
  CHECK(rig.dinBytes() == std::vector<uint8_t>({0x80, 60, 0, 0x80, 64, 0, 0x80, 67, 0}));
  CHECK(rig.tracker.heldCount(MidiOutput::Din) == 0);
  rig.play({0x90, 60, 100}); // first inversion
  CHECK(noteOns(rig.dinBytes()) == std::vector<uint8_t>({64, 67, 72}));
  rig.play({0x80, 60, 0});
  rig.dinBytes();

  // Diatonic chords come from the scale: D in C major is D minor
  int8_t offsets[FxEngine::kMaxOut];
  FxChainConfig d;
  d.chord = FxChord::Diatonic;
  d.scaleId = MusicTheory::ScaleMajor;
  CHECK(FxEngine::chordOffsets(d, 62, offsets) == 3 && offsets[1] == 3 && offsets[2] == 7);
  CHECK(FxEngine::chordOffsets(d, 60, offsets) == 3 && offsets[1] == 4 && offsets[2] == 7); // C major
  d.chordOpen = true;
  CHECK(FxEngine::chordOffsets(d, 60, offsets) == 3 && offsets[0] == 0 && offsets[1] == 7 && offsets[2] == 16);
  // Scale first, then the chord root is in the scale
  enable(c, FxSlot::Scale);
  c.chordInversion = 0;
  c.scaleId = MusicTheory::ScaleMajor;
  c.chord = FxChord::Diatonic;
  rig.apply();
  rig.play({0x90, 63, 100}); // Eb -> D (nearest, tie down), D minor
  CHECK(noteOns(rig.dinBytes()) == std::vector<uint8_t>({62, 65, 69}));
}

TEST(testFxArp)
{
  FxRig rig;
  FxChainConfig &c = rig.usb();
  enable(c, FxSlot::Arp);
  c.arpGate = 50;
  rig.apply();

  // Transport stopped: notes pass straight through
  rig.play({0x90, 50, 100});
  CHECK(noteOns(rig.dinBytes()) == std::vector<uint8_t>({50}));
  rig.play({0x80, 50, 0});
  rig.dinBytes();

  rig.clock.config().outputs[0].enabled = false; // keep clock bytes out of the DIN capture
  rig.clock.config().outputs[0].transport = false;
  rig.clock.changed();
  rig.clock.start();
  rig.runFor(2000);
  rig.play({0x90, 60, 100});
  rig.play({0x90, 64, 100});
  rig.play({0x90, 67, 100});
  CHECK(rig.dinBytes().empty()); // held for the arp, nothing straight out
  rig.runFor(130000 * 6);        // six 16ths at 120 BPM
  std::vector<uint8_t> out = rig.dinBytes();
  std::vector<uint8_t> ons = noteOns(out);
  CHECK(ons.size() >= 6);
  if (ons.size() >= 6) CHECK(std::vector<uint8_t>(ons.begin(), ons.begin() + 6) == std::vector<uint8_t>({60, 64, 67, 60, 64, 67}));
  CHECK(rig.tracker.heldCount(MidiOutput::Din) <= 1); // gates end each arp note

  // Releasing the keys ends the arp (no latch)
  rig.play({0x80, 60, 0});
  rig.play({0x80, 64, 0});
  rig.play({0x80, 67, 0});
  rig.runFor(130000 * 4);
  rig.dinBytes();
  rig.runFor(130000 * 4);
  CHECK(noteOns(rig.dinBytes()).empty());
  CHECK(rig.tracker.heldCount(MidiOutput::Din) == 0);

  // Latch: the notes keep going after release; Stop ends it
  c.arpLatch = true;
  c.arpMode = FxArpMode::Down;
  rig.apply();
  rig.play({0x90, 60, 100});
  rig.play({0x90, 72, 100});
  rig.play({0x80, 60, 0});
  rig.play({0x80, 72, 0});
  rig.runFor(130000 * 4);
  ons = noteOns(rig.dinBytes());
  CHECK(ons.size() >= 3 && ons[0] == 72 && ons[1] == 60);
  rig.clock.stop();
  rig.runFor(130000 * 4);
  rig.dinBytes();
  rig.runFor(130000 * 4);
  CHECK(noteOns(rig.dinBytes()).empty());
  CHECK(rig.tracker.heldCount(MidiOutput::Din) == 0);
}

TEST(testFxArpOffReleasesItsNote)
{
  FxRig rig;
  FxChainConfig &c = rig.usb();
  enable(c, FxSlot::Arp);
  c.arpGate = 100;
  c.arpRate = FxArpRate::R4;
  rig.apply();
  rig.clock.config().outputs[0].enabled = false;
  rig.clock.config().outputs[0].transport = false;
  rig.clock.changed();
  rig.clock.start();
  rig.runFor(2000);
  rig.play({0x90, 60, 100});
  rig.runFor(150000); // the arp note sounds (quarter-note gate)
  CHECK(rig.tracker.isHeld(MidiOutput::Din, 0, 60));
  c.enabled = 0;
  rig.apply();
  CHECK(!rig.tracker.isHeld(MidiOutput::Din, 0, 60));
}

TEST(testFxVelocityAndCc)
{
  FxChainConfig c;
  c.velCurve = FxVelCurve::Linear;
  c.velMin = 40;
  c.velMax = 100;
  CHECK(FxEngine::curveVelocity(c, 127, 0) == 100 && FxEngine::curveVelocity(c, 1, 0) == 40);
  c.velCurve = FxVelCurve::Fixed;
  c.velFixed = 99;
  CHECK(FxEngine::curveVelocity(c, 5, 0) == 99);
  c.velCurve = FxVelCurve::Soft;
  c.velMin = 1;
  c.velMax = 127;
  CHECK(FxEngine::curveVelocity(c, 32, 0) > 32);
  c.velCurve = FxVelCurve::Hard;
  CHECK(FxEngine::curveVelocity(c, 64, 0) < 64);
  c.ccMin = 20;
  c.ccMax = 60;
  CHECK(FxEngine::mapCc(c, 0) == 20 && FxEngine::mapCc(c, 127) == 60);
  c.ccInvert = true;
  CHECK(FxEngine::mapCc(c, 0) == 60);

  // CC mapping and smoothing through the router
  FxRig rig;
  FxChainConfig &u = rig.usb();
  enable(u, FxSlot::Cc);
  u.ccSource = 1;
  u.ccTarget = 74;
  rig.apply();
  rig.play({0xB3, 1, 100});
  CHECK(rig.dinBytes() == std::vector<uint8_t>({0xB3, 74, 100}));
  rig.play({0xB3, 7, 50}); // other CCs pass unchanged
  CHECK(rig.dinBytes() == std::vector<uint8_t>({0xB3, 7, 50}));
  u.ccSmooth = 10; // full range in 100 ms
  rig.apply();
  rig.play({0xB3, 1, 0});
  rig.runFor(200000);
  std::vector<uint8_t> out = rig.dinBytes();
  CHECK(out.size() >= 6 && out[out.size() - 1] == 0 && out[out.size() - 2] == 74);
  size_t sent = out.size();
  rig.runFor(100000);
  CHECK(rig.dinBytes().empty() && sent <= 3 * 12); // reached the target and stopped
}

TEST(testFxPassThroughAndRouting)
{
  FxRig rig;
  enable(rig.usb(), FxSlot::Transpose);
  rig.usb().transpose = 3;
  rig.apply();
  rig.play({0xC4, 9});
  rig.play({0xE4, 0, 70});
  rig.play({0xD4, 33});
  CHECK(rig.dinBytes() == std::vector<uint8_t>({0xC4, 9, 0xE4, 0, 70, 0xD4, 33}));
  // A routing change releases the input's notes and the chain forgets them
  rig.play({0x90, 60, 100});
  rig.dinBytes();
  rig.router.config().forceChannel[0] = 5;
  rig.router.changed();
  rig.router.applyPending();
  CHECK(rig.dinBytes() == std::vector<uint8_t>({0x80, 63, 0}));
  CHECK(rig.fx.heldCount(MidiSource::Usb) == 0);
  // Blocked by the router: nothing reaches the effects
  rig.router.config().channelMask[0] = 0;
  rig.router.changed();
  CHECK(!rig.play({0x90, 60, 100}));
  CHECK(rig.dinBytes().empty());
}

TEST(testFxFreshEngineIsSilent)
{
  // A new engine sends nothing on its own (no stale pending note-offs)
  FxRig rig;
  rig.fx.run();
  rig.runFor(10000);
  CHECK(rig.dinBytes().empty());
}

TEST(testFxPresets)
{
  FxRig rig;
  FxConfig &c = rig.fx.config();
  c.chains[0].transpose = 7;
  rig.fx.savePreset(2);
  c.chains[0].transpose = 0;
  rig.fx.loadPreset(2);
  CHECK(c.chains[0].transpose == 7 && c.preset == 2);
  rig.fx.loadPreset(0);
  CHECK(c.chains[0].transpose == 0);
}
