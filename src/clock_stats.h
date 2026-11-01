#pragma once

#include <stdint.h>

/*! What the analyser shows for one input (stage 9). Times in ms unless noted. */
struct ClockStatsSnapshot
{
  enum Status : uint8_t
  {
    Off = 0,    ///< no clock for 2 s
    Stable = 1, ///< jitter sd < 0.5 ms and max deviation < 1.5 ms
    Jittery = 2,
    Bad = 3, ///< dropout in the last 2 s, or sd >= 2 ms
  };

  bool active;          ///< a tick in the last 2 s
  uint32_t ticks;       ///< since reset
  uint32_t intervals;   ///< intervals in the statistics (batched ones and dropouts excluded)
  float bpmNow;         ///< from the last 24 intervals
  float bpmAvg;         ///< from the rolling window (96 ticks = 4 beats)
  float bpmSession;     ///< since reset
  float intervalAvg, intervalMin, intervalMax; ///< since reset
  float jitterSd;       ///< rolling window
  float maxDeviation;   ///< rolling window, signed: the largest |interval - mean|
  uint32_t dropouts;
  float longestGap;
  uint32_t batched;     ///< USB: several ticks in one transfer (spacing unknown)
  bool doubleClock;     ///< alternating short/long intervals: two clocks merged?
  bool running;         ///< transport state from Start/Continue/Stop
  uint32_t ticksThisRun;
  uint32_t starts, continues, stops, songPositions;
  uint16_t lastSongPosition; ///< in 16ths
  bool songPositionMismatch; ///< Continue didn't resume where the song position said
  float startToFirstTick; ///< ms from Start to the first tick (MIDI says it should be > 1 ms)
  float driftBpmPerMinute; ///< slope of the per-beat BPM over the beat history
  Status status;
};

/*! Clock statistics for one input. Pure C++ (no Arduino), so it runs in the host tests.
 *  Fed from the MIDI task; the caller keeps it consistent with readers (see ClockAnalyser). */
class ClockStats
{
public:
  static constexpr int kRolling = 96;    ///< rolling window: 4 beats
  static constexpr int kHistory = 1024;  ///< intervals kept for the histogram
  static constexpr int kBeats = 300;     ///< per-beat BPM history for the drift view
  static constexpr int kBins = 64;
  static constexpr uint32_t kActiveMillis = 2000;
  static constexpr uint32_t kBatchedMicros = 200; ///< closer ticks came in one USB transfer

  void reset();
  /*! A clock or transport message (F8, FA, FB, FC, F2) at timeMicros */
  void onMessage(const uint8_t *bytes, uint16_t length, uint32_t timeMicros, uint32_t nowMillis);
  ClockStatsSnapshot snapshot(uint32_t nowMillis) const;

  /*! Histogram of deviations from the mean over the kept history, auto-ranged to
   *  +-1/2/5/10 ms; returns the range in ms and the number of intervals counted */
  int histogram(uint16_t bins[kBins], float &rangeMs) const;
  /*! Per-beat BPM, oldest first; `dropout[i]` marks beats that contained a dropout */
  int beats(float *bpm, bool *dropout, int max) const;
  float meanIntervalMicros() const { return _rollingCount ? (float)(_rollingSum / _rollingCount) : 0; }
  uint32_t lastTickMicros() const { return _lastTick; }
  bool hasTicks() const { return _ticks > 0; }
  uint32_t ticks() const { return _ticks; }

private:
  void tick(uint32_t timeMicros, uint32_t nowMillis);
  void addInterval(uint32_t interval);

  // Since reset
  uint32_t _ticks = 0;
  uint32_t _lastTick = 0;
  uint32_t _lastTickMillis = 0;
  uint32_t _intervalCount = 0;
  double _mean = 0, _m2 = 0; // Welford (kept for completeness of the session average)
  uint32_t _min = 0, _max = 0;
  uint32_t _dropouts = 0, _longestGap = 0, _lastDropoutMillis = 0;
  uint32_t _batched = 0;

  // Rolling window
  uint32_t _rolling[kRolling] = {0};
  int _rollingNext = 0, _rollingCount = 0;
  uint64_t _rollingSum = 0;

  // History for the histogram
  uint32_t _history[kHistory] = {0};
  int _historyNext = 0, _historyCount = 0;

  // Per-beat BPM
  float _beatBpm[kBeats] = {0};
  bool _beatDropout[kBeats] = {false};
  int _beatNext = 0, _beatCount = 0;
  uint32_t _beatStart = 0;
  int _beatTicks = 0;
  bool _beatHadDropout = false;
  uint32_t _beatStartMillis = 0;
  uint32_t _beatMillis[kBeats] = {0}; // when each beat ended (for the drift slope)

  // Transport
  bool _running = false;
  uint32_t _ticksThisRun = 0;
  uint32_t _starts = 0, _continues = 0, _stops = 0, _songPositions = 0;
  uint16_t _songPosition = 0;
  bool _positionPending = false; // a song position arrived while stopped
  bool _positionMismatch = false;
  uint32_t _runTicksAtStop = 0;
  uint32_t _startTime = 0;
  bool _awaitFirstTick = false;
  float _startToFirstTick = 0;
};

/*! DIN-minus-USB phase of two clocks at the same tempo (stage 9 Phase view) */
class ClockPhase
{
public:
  static constexpr int kRolling = 96;
  static constexpr int kHistory = 300;     ///< one value per beat
  static constexpr uint32_t kDinByteMicros = 320; ///< a DIN tick is stamped after its byte

  void reset();
  /*! A DIN tick; `usb` gives the latest USB tick and its interval */
  void onDinTick(uint32_t dinMicros, const ClockStats &usb, const ClockStats &din);
  bool valid() const { return _count > 0; }
  float meanMs() const;
  float sdMs() const;
  int history(float *phaseMs, int max) const;

private:
  float _rolling[kRolling] = {0};
  int _next = 0, _count = 0;
  float _beatHistory[kHistory] = {0};
  int _historyNext = 0, _historyCount = 0;
  int _ticksInBeat = 0;
};
