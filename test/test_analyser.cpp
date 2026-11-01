// Stage 9: clock analyser statistics, fed with synthetic clocks
#include "test_support.h"
#include "clock_analyser.h"
#include "clock_stats.h"

static constexpr uint32_t kTick120 = 20833; // 120 BPM

/*! Feed `count` ticks starting at `t` with the given interval plus a deterministic spread */
static uint32_t feedTicks(ClockStats &stats, uint32_t t, int count, uint32_t interval, int jitter = 0)
{
  static const uint8_t clock = 0xF8;
  uint32_t seed = 12345;
  for (int i = 0; i < count; i++) {
    seed = seed * 1103515245 + 12345; // LCG: repeatable pseudo-random offsets in +-jitter
    int offset = jitter ? (int)((seed >> 8) % (2 * jitter + 1)) - jitter : 0;
    stats.onMessage(&clock, 1, t + offset, (t + offset) / 1000);
    t += interval;
  }
  return t;
}

TEST(testClockStatsSteady)
{
  static ClockStats stats;
  stats.reset();
  uint32_t t = feedTicks(stats, 1000000, 200, kTick120, 200);
  ClockStatsSnapshot s = stats.snapshot(t / 1000);
  CHECK(s.active && s.ticks == 200 && s.intervals == 199);
  CHECK(fabsf(s.bpmAvg - 120) < 0.2f && fabsf(s.bpmSession - 120) < 0.2f);
  CHECK(s.jitterSd > 0.05f && s.jitterSd < 0.3f);
  CHECK(fabsf(s.maxDeviation) <= 0.41f);
  CHECK(s.intervalMin >= 20.4f && s.intervalMax <= 21.3f);
  CHECK(s.status == ClockStatsSnapshot::Stable && !s.doubleClock && s.dropouts == 0);
  CHECK(stats.snapshot(t / 1000 + 3000).status == ClockStatsSnapshot::Off); // nothing for 3 s

  uint16_t bins[ClockStats::kBins];
  float range;
  CHECK(stats.histogram(bins, range) == 199 && range == 1.0f);
  int middle = 0;
  for (int i = 19; i <= 45; i++) middle += bins[i];
  CHECK(middle == 199); // +-0.2 ms offsets: every interval within +-0.4 ms of the centre
}

TEST(testClockStatsDropoutAndJitter)
{
  static ClockStats stats;
  stats.reset();
  uint32_t t = feedTicks(stats, 0, 50, kTick120);
  t = feedTicks(stats, t + 100000, 50, kTick120); // 100 ms gap
  ClockStatsSnapshot s = stats.snapshot(t / 1000);
  CHECK(s.dropouts == 1 && s.longestGap > 120 && s.longestGap < 122);
  CHECK(s.intervals == 98); // the gap isn't in the statistics
  CHECK(fabsf(s.bpmAvg - 120) < 0.1f);
  CHECK(s.status == ClockStatsSnapshot::Bad);

  stats.reset();
  t = feedTicks(stats, 0, 200, kTick120, 4000); // +-4 ms: sd above 2 ms
  CHECK(stats.snapshot(t / 1000).status == ClockStatsSnapshot::Bad);
  stats.reset();
  t = feedTicks(stats, 0, 200, kTick120, 1500);
  CHECK(stats.snapshot(t / 1000).status == ClockStatsSnapshot::Jittery);
}

TEST(testClockStatsDoubleClockAndBatching)
{
  static ClockStats stats;
  stats.reset();
  static const uint8_t clock = 0xF8;
  uint32_t t = 0;
  for (int i = 0; i < 100; i++) { // two 120 BPM clocks merged 5 ms apart
    stats.onMessage(&clock, 1, t, t / 1000);
    stats.onMessage(&clock, 1, t + 5000, (t + 5000) / 1000);
    t += kTick120;
  }
  ClockStatsSnapshot s = stats.snapshot(t / 1000);
  CHECK(s.doubleClock && s.status != ClockStatsSnapshot::Stable);

  // USB: pairs of ticks in one transfer have the same stamp
  stats.reset();
  t = 0;
  for (int i = 0; i < 50; i++) {
    stats.onMessage(&clock, 1, t, t / 1000);
    stats.onMessage(&clock, 1, t, t / 1000);
    t += 2 * kTick120;
  }
  s = stats.snapshot(t / 1000);
  CHECK(s.batched == 50 && s.intervals == 49);
}

TEST(testClockStatsTransport)
{
  static ClockStats stats;
  stats.reset();
  static const uint8_t start = 0xFA, stop = 0xFC, cont = 0xFB;
  stats.onMessage(&start, 1, 1000, 1);
  uint32_t t = feedTicks(stats, 3000, 96, kTick120);
  ClockStatsSnapshot s = stats.snapshot(t / 1000);
  CHECK(s.running && s.starts == 1 && s.ticksThisRun == 96);
  CHECK(fabsf(s.startToFirstTick - 2.0f) < 0.01f);
  stats.onMessage(&stop, 1, t, t / 1000);
  uint8_t position[3] = {0xF2, 32, 0}; // bar 3 (32 sixteenths)
  stats.onMessage(position, 3, t, t / 1000);
  stats.onMessage(&cont, 1, t, t / 1000);
  s = stats.snapshot(t / 1000);
  CHECK(!s.songPositionMismatch && s.lastSongPosition == 32 && s.ticksThisRun == 192 && s.running);
  stats.onMessage(position, 3, t, t / 1000); // while running: not allowed
  CHECK(stats.snapshot(t / 1000).songPositionMismatch);
}

TEST(testClockStatsDrift)
{
  static ClockStats stats;
  stats.reset();
  static const uint8_t clock = 0xF8;
  // 120 -> 122 BPM over two minutes
  double t = 0;
  for (int i = 0; i < 24 * 240; i++) {
    double bpm = 120 + 2.0 * t / 120e6;
    stats.onMessage(&clock, 1, (uint32_t)t, (uint32_t)(t / 1000));
    t += 60e6 / (24 * bpm);
  }
  ClockStatsSnapshot s = stats.snapshot((uint32_t)(t / 1000));
  CHECK(s.driftBpmPerMinute > 0.8f && s.driftBpmPerMinute < 1.2f);
  static float bpm[ClockStats::kBeats];
  static bool dropout[ClockStats::kBeats];
  int n = stats.beats(bpm, dropout, ClockStats::kBeats);
  CHECK(n == 239 && bpm[0] < bpm[n - 1] && !dropout[0]);
}

TEST(testClockPhaseAndAnalyser)
{
  static ClockAnalyser analyser;
  analyser.reset();
  analyser.setCsv(true);
  uint8_t clock = 0xF8;
  uint32_t t = 0;
  for (int i = 0; i < 100; i++) { // DIN stamped 3 ms (+ one byte) after USB
    analyser.onMessage({MidiSource::Usb, 0, &clock, 1, t}, t / 1000);
    analyser.onMessage({MidiSource::Din, 0, &clock, 1, t + 3000 + 320}, t / 1000);
    t += kTick120;
  }
  ClockPhase phase = analyser.phase();
  CHECK(phase.valid() && fabsf(phase.meanMs() - 3.0f) < 0.01f && phase.sdMs() < 0.01f);
  static float history[ClockPhase::kHistory];
  CHECK(phase.history(history, ClockPhase::kHistory) == 4);
  CHECK(analyser.copy(MidiSource::Din).ticks() == 100);

  ClockAnalyser::CsvRecord record;
  size_t records = 0;
  bool intervalsOk = true;
  while (analyser.popCsv(record)) {
    records++;
    if (record.tick > 1 && record.interval != kTick120) intervalsOk = false;
  }
  CHECK(records == ClockAnalyser::kCsvLength - 1 && intervalsOk); // the ring keeps the oldest 127
  analyser.reset();
  CHECK(analyser.copy(MidiSource::Din).ticks() == 0 && !analyser.phase().valid());
}
