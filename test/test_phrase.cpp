// Stage 22: phrase tracker
#include "test_support.h"

#include "cue_queue.h"
#define private public
#include "phrase_tracker.h"
#undef private

namespace
{
constexpr double kPeriod = 20833.333; // one 24 PPQN tick at 120 BPM

/*! A tracker fed directly with clock events (no engine) */
struct PhraseRig
{
  PhraseTracker tracker;
  uint64_t timeUs = 1000000;
  uint8_t beats = 4;
  bool external = false;

  void event(ClockEvent::Type type, uint32_t tick = 0)
  {
    tracker._external = external;
    tracker.handle({type, tick, 0, timeUs}, beats, kPeriod);
  }
  void ticks(uint32_t count)
  {
    for (uint32_t i = 0; i < count; i++) {
      event(ClockEvent::RawTick);
      timeUs += (uint64_t)kPeriod;
    }
  }
  void bars(uint32_t count) { ticks(count * 24u * beats); }
  PhraseSnapshot snap() { return tracker.snapshot(); }
  void apply(const PhraseConfig &config)
  {
    tracker.config() = config;
    tracker.changed();
    tracker.run();
  }
  /*! Start time of the bar whose downbeat was the last tick counted */
  uint64_t lastTickUs() const { return timeUs - (uint64_t)kPeriod; }
};

void drainCues()
{
  Cues::Item item;
  while (Cues::queue().take(item)) {
  }
}
} // namespace

TEST(testPhraseCounting)
{
  gFakeNvs.clear();
  PhraseRig rig;
  rig.event(ClockEvent::Start);
  rig.ticks(1); // bar 1, beat 1
  PhraseSnapshot s = rig.snap();
  CHECK(s.running && s.synced && s.counting && s.barInPhrase == 0 && s.barsLeft == 16 && s.phraseBars == 16);

  rig.bars(12); // the downbeat of bar 13 is the next tick
  rig.ticks(1);
  s = rig.snap();
  CHECK(s.barInPhrase == 12 && s.barsLeft == 4 && !s.warning && s.phrase == 0);
  rig.bars(3); // bar 16: the warning bar
  s = rig.snap();
  CHECK(s.barInPhrase == 15 && s.barsLeft == 1 && s.warning);
  rig.bars(1); // bar 1 of phrase 2
  s = rig.snap();
  CHECK(s.barInPhrase == 0 && s.phrase == 1 && s.phraseInSection == 1 && !s.warning);
  rig.bars(16 * 3); // 4 phrases on: the section wraps
  s = rig.snap();
  CHECK(s.phrase == 4 && s.phraseInSection == 0);
  CHECK(s.nextPhraseTick == (uint32_t)(16 * 5) * 96);

  // Changing the phrase length takes effect at once: bar 65 is bar 1 of an 8-bar phrase too
  PhraseConfig config;
  config.phraseBars = 8;
  config.sectionPhrases = 0;
  rig.apply(config);
  s = rig.snap();
  CHECK(s.phraseBars == 8 && s.barInPhrase == 0 && s.phrase == 8 && s.phraseInSection == 0);

  // 3/4 time
  PhraseRig waltz;
  waltz.beats = 3;
  waltz.event(ClockEvent::Start);
  waltz.ticks(1);
  waltz.bars(5);
  s = waltz.snap();
  CHECK(s.barInPhrase == 5 && s.beatsPerBar == 3 && s.beatInBar == 0);
  waltz.ticks(24);
  CHECK(waltz.snap().beatInBar == 1);
}

TEST(testPhraseTransport)
{
  gFakeNvs.clear();
  PhraseRig rig;
  // Internal clock while stopped: ticks arrive but nothing counts
  rig.ticks(200);
  PhraseSnapshot s = rig.snap();
  CHECK(!s.counting && !s.synced && s.bar == 0);

  rig.event(ClockEvent::Start);
  rig.ticks(1);
  rig.bars(5);
  rig.event(ClockEvent::Stop);
  rig.bars(3); // internal clock keeps ticking: the position holds
  CHECK(rig.snap().barInPhrase == 5 && !rig.snap().running);
  rig.event(ClockEvent::Continue);
  rig.bars(1);
  CHECK(rig.snap().barInPhrase == 6);

  // Song Position: 16th 64 = bar 5 in 4/4; realigns (offset 0)
  rig.tracker._offsetBars = 3;
  rig.event(ClockEvent::Stop);
  rig.event(ClockEvent::Locate, 64 * 6);
  rig.event(ClockEvent::Continue);
  rig.ticks(1);
  s = rig.snap();
  CHECK(s.bar == 4 && s.barInPhrase == 4 && s.synced);

  // With "SPP ignore" the manual offset is kept
  PhraseConfig config;
  config.sppRealigns = false;
  rig.apply(config);
  rig.tracker._offsetBars = 2;
  rig.event(ClockEvent::Stop);
  rig.event(ClockEvent::Locate, 64 * 6);
  rig.event(ClockEvent::Continue);
  rig.ticks(1);
  CHECK(rig.snap().bar == 2);

  // Start resets the offset
  rig.event(ClockEvent::Start);
  rig.ticks(1);
  CHECK(rig.snap().bar == 0);
}

TEST(testPhraseFreeRunningAndRealign)
{
  gFakeNvs.clear();
  uint64_t barUs = (uint64_t)(96 * kPeriod);

  PhraseRig rig;
  rig.external = true; // a master that was already playing: ticks but no Start
  rig.bars(7);
  rig.ticks(1); // downbeat of bar 8
  PhraseSnapshot s = rig.snap();
  CHECK(s.counting && !s.synced && s.barInPhrase == 7);

  // Bar 1 tap 10 % into the current bar: this bar becomes bar 1
  uint64_t barStart = rig.lastTickUs();
  rig.tracker.realign(barStart + barUs / 10);
  rig.tracker.run();
  s = rig.snap();
  CHECK(s.synced && s.barInPhrase == 0);
  // 40 %: still this bar
  rig.tracker.realign(barStart + barUs * 4 / 10);
  rig.tracker.run();
  CHECK(rig.snap().barInPhrase == 0);

  // 60 % and 90 % (tapped early for the next bar): the next bar becomes bar 1, so the
  // current one is the last of the phrase
  rig.ticks(60); // well into the bar, the tap time is what counts
  rig.tracker.realign(barStart + barUs * 6 / 10);
  rig.tracker.run();
  CHECK(rig.snap().barInPhrase == 15);
  rig.tracker.realign(barStart + barUs * 9 / 10);
  rig.tracker.run();
  CHECK(rig.snap().barInPhrase == 15);
  rig.ticks(36); // the next downbeat
  CHECK(rig.snap().barInPhrase == 0);

  // Stop from the master: its ticks no longer move the position
  rig.event(ClockEvent::Stop);
  int32_t bar = rig.snap().bar;
  rig.bars(2);
  CHECK(rig.snap().bar == bar && !rig.snap().counting);

  // Phrase-only realign keeps the section count
  PhraseRig section;
  section.event(ClockEvent::Start);
  section.ticks(1);
  section.bars(16 * 2 + 3);
  CHECK(section.snap().phraseInSection == 2 && section.snap().barInPhrase == 3);
  section.tracker.realign(section.lastTickUs(), true);
  section.tracker.run();
  s = section.snap();
  CHECK(s.barInPhrase == 0 && s.phraseInSection == 2);

  // Reset: the current bar becomes bar 1
  section.bars(2);
  section.tracker.reset();
  section.tracker.run();
  CHECK(section.snap().barInPhrase == 0);
}

TEST(testPhraseCuesAndBoundaries)
{
  gFakeNvs.clear();
  drainCues();
  PhraseRig rig;
  PhraseConfig config;
  config.phraseBars = 4;
  config.warnBars = 1;
  rig.apply(config);

  static int boundaries = 0;
  boundaries = 0;
  rig.tracker.addBoundaryListener([](uint16_t, uint64_t, void *) { boundaries++; }, nullptr);

  rig.event(ClockEvent::Start);
  Cues::Item item;
  CHECK(Cues::queue().take(item) && item.cue == Cue::PhraseStart && item.dueMicros == 0);

  uint64_t start = rig.timeUs;
  rig.ticks(1); // bar 1 downbeat: a phrase boundary
  CHECK(boundaries == 1);
  CHECK(!Cues::queue().take(item)); // bar 2 is neither a warning nor a phrase start
  rig.bars(2);                      // bars 2 and 3; the downbeat of bar 3 posts the bar 4 warning
  CHECK(Cues::queue().take(item) && item.cue == Cue::PhraseEnding);
  uint32_t bar4 = (uint32_t)(start + 3 * (uint64_t)(96 * kPeriod));
  CHECK(item.dueMicros + 200 >= bar4 && item.dueMicros <= bar4 + 200);
  rig.bars(1); // the downbeat of bar 4 posts the phrase start of bar 5
  CHECK(Cues::queue().take(item) && item.cue == Cue::PhraseStart && item.dueMicros > 0);
  rig.ticks(96); // bar 5 downbeat
  CHECK(boundaries == 2);

  // Not synced: no cues
  drainCues();
  PhraseRig unsynced;
  unsynced.external = true;
  unsynced.bars(20);
  CHECK(!Cues::queue().take(item));
}

TEST(testPhraseWithClockEngine)
{
  // The tracker as a real ClockEngine listener: internal clock, Start, two 4-bar phrases
  OutputRig rig;
  gRunRig = &rig;
  PhraseTracker tracker;
  tracker.begin(rig.clock);
  PhraseConfig config;
  config.phraseBars = 4;
  tracker.config() = config;
  tracker.changed();
  tracker.run();
  runFor(rig.clock, 500000);
  CHECK(!tracker.snapshot().counting); // stopped: internal ticks don't count
  rig.clock.start();
  runFor(rig.clock, 8 * 2000000 + 100000); // just past 8 bars at 120 BPM (2 s each)
  PhraseSnapshot s = tracker.snapshot();
  CHECK(s.running && s.synced && s.phrase == 2 && s.barInPhrase == 0);
  gRunRig = nullptr;
  drainCues();
}
