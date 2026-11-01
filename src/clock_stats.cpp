#include "clock_stats.h"

#include <math.h>
#include <string.h>

// All members start at zero, so a reset is a memset (no 7 KB temporary on a small task stack)
void ClockStats::reset() { memset(this, 0, sizeof(*this)); }

void ClockStats::onMessage(const uint8_t *bytes, uint16_t length, uint32_t timeMicros, uint32_t nowMillis)
{
  switch (bytes[0]) {
    case 0xF8:
      tick(timeMicros, nowMillis);
      break;
    case 0xFA: // start: the first tick after it is beat 1
      _starts++;
      _running = true;
      _ticksThisRun = 0;
      _startTime = timeMicros;
      _awaitFirstTick = true;
      _positionPending = false;
      break;
    case 0xFB:
      _continues++;
      _running = true;
      _positionPending = false;
      break;
    case 0xFC:
      _stops++;
      _running = false;
      _runTicksAtStop = _ticksThisRun;
      break;
    case 0xF2:
      if (length < 3) break;
      _songPositions++;
      _songPosition = (bytes[2] << 7) | bytes[1];
      if (_running) {
        _positionMismatch = true; // the spec only allows Song Position while stopped
      } else {
        _positionPending = true;
        _ticksThisRun = _songPosition * 6u; // 1 song position unit = a 16th = 6 ticks
      }
      break;
  }
}

void ClockStats::addInterval(uint32_t interval)
{
  _intervalCount++;
  if (_intervalCount == 1 || interval < _min) _min = interval;
  if (interval > _max) _max = interval;
  double delta = interval - _mean;
  _mean += delta / _intervalCount;
  _m2 += delta * (interval - _mean);

  if (_rollingCount == kRolling) _rollingSum -= _rolling[_rollingNext];
  else _rollingCount++;
  _rolling[_rollingNext] = interval;
  _rollingSum += interval;
  _rollingNext = (_rollingNext + 1) % kRolling;

  _history[_historyNext] = interval;
  _historyNext = (_historyNext + 1) % kHistory;
  if (_historyCount < kHistory) _historyCount++;
}

void ClockStats::tick(uint32_t timeMicros, uint32_t nowMillis)
{
  _ticks++;
  _lastTickMillis = nowMillis ? nowMillis : 1;
  if (_running) _ticksThisRun++;
  if (_awaitFirstTick) {
    _startToFirstTick = (uint32_t)(timeMicros - _startTime) / 1000.0f;
    _awaitFirstTick = false;
  }

  if (_ticks > 1) {
    uint32_t interval = timeMicros - _lastTick; // unsigned: survives the 71-minute micros() wrap
    if (interval < kBatchedMicros) {
      _batched++; // several ticks in one USB transfer: their real spacing is unknown
    } else if (_rollingCount >= 8 && interval > 3 * meanIntervalMicros()) {
      _dropouts++;
      if (interval > _longestGap) _longestGap = interval;
      _lastDropoutMillis = _lastTickMillis;
      _beatHadDropout = true;
    } else {
      addInterval(interval);
    }
  }
  _lastTick = timeMicros;

  // Per-beat BPM: 24 intervals per quarter note
  if (_beatTicks == 0) {
    _beatStart = timeMicros;
    _beatStartMillis = nowMillis;
  }
  _beatTicks++;
  if (_beatTicks == 25) {
    uint32_t duration = timeMicros - _beatStart;
    _beatBpm[_beatNext] = duration ? 60e6f / duration : 0;
    _beatDropout[_beatNext] = _beatHadDropout;
    _beatMillis[_beatNext] = nowMillis;
    _beatNext = (_beatNext + 1) % kBeats;
    if (_beatCount < kBeats) _beatCount++;
    _beatStart = timeMicros;
    _beatStartMillis = nowMillis;
    _beatTicks = 1;
    _beatHadDropout = false;
  }
}

ClockStatsSnapshot ClockStats::snapshot(uint32_t nowMillis) const
{
  ClockStatsSnapshot s = {};
  s.active = _ticks && nowMillis - _lastTickMillis < kActiveMillis;
  s.ticks = _ticks;
  s.intervals = _intervalCount;
  s.dropouts = _dropouts;
  s.longestGap = _longestGap / 1000.0f;
  s.batched = _batched;
  s.running = _running;
  s.ticksThisRun = _ticksThisRun;
  s.starts = _starts;
  s.continues = _continues;
  s.stops = _stops;
  s.songPositions = _songPositions;
  s.lastSongPosition = _songPosition;
  s.songPositionMismatch = _positionMismatch;
  s.startToFirstTick = _startToFirstTick;

  if (_intervalCount) {
    s.intervalAvg = _mean / 1000.0;
    s.intervalMin = _min / 1000.0f;
    s.intervalMax = _max / 1000.0f;
    s.bpmSession = 60e6 / (24 * _mean);
  }

  if (_rollingCount) {
    double mean = (double)_rollingSum / _rollingCount;
    s.bpmAvg = 60e6 / (24 * mean);
    double sumSquares = 0;
    double maxDeviation = 0;
    for (int i = 0; i < _rollingCount; i++) {
      double deviation = _rolling[i] - mean;
      sumSquares += deviation * deviation;
      if (fabs(deviation) > fabs(maxDeviation)) maxDeviation = deviation;
    }
    s.jitterSd = sqrt(sumSquares / _rollingCount) / 1000.0;
    s.maxDeviation = maxDeviation / 1000.0;

    // Instant tempo from the newest 24 intervals
    int n = _rollingCount < 24 ? _rollingCount : 24;
    uint64_t recent = 0;
    for (int i = 1; i <= n; i++) recent += _rolling[(_rollingNext - i + kRolling) % kRolling];
    s.bpmNow = 60e6 / (24.0 * recent / n);

    // Two merged clocks show as alternating short and long intervals
    int alternating = 0;
    for (int i = 1; i < _rollingCount; i++) {
      double a = _rolling[(_rollingNext - i - 1 + 2 * kRolling) % kRolling];
      double b = _rolling[(_rollingNext - i + kRolling) % kRolling];
      if ((a < 0.75 * mean && b > 1.25 * mean) || (a > 1.25 * mean && b < 0.75 * mean)) alternating++;
    }
    s.doubleClock = _rollingCount >= 16 && alternating * 3 > _rollingCount;
  }

  // Drift: least-squares slope of per-beat BPM against time
  if (_beatCount >= 8) {
    double sx = 0, sy = 0, sxx = 0, sxy = 0;
    int first = (_beatNext - _beatCount + kBeats) % kBeats;
    uint32_t t0 = _beatMillis[first];
    for (int i = 0; i < _beatCount; i++) {
      int k = (first + i) % kBeats;
      double x = (uint32_t)(_beatMillis[k] - t0) / 60000.0; // minutes
      double y = _beatBpm[k];
      sx += x;
      sy += y;
      sxx += x * x;
      sxy += x * y;
    }
    double denominator = _beatCount * sxx - sx * sx;
    if (denominator > 1e-9) s.driftBpmPerMinute = (_beatCount * sxy - sx * sy) / denominator;
  }

  if (!s.active) {
    s.status = ClockStatsSnapshot::Off;
  } else if ((_lastDropoutMillis && nowMillis - _lastDropoutMillis < kActiveMillis) || s.jitterSd >= 2.0f) {
    s.status = ClockStatsSnapshot::Bad;
  } else if (s.jitterSd < 0.5f && fabsf(s.maxDeviation) < 1.5f && !s.doubleClock) {
    s.status = ClockStatsSnapshot::Stable;
  } else {
    s.status = ClockStatsSnapshot::Jittery;
  }
  return s;
}

int ClockStats::histogram(uint16_t bins[kBins], float &rangeMs) const
{
  memset(bins, 0, kBins * sizeof(uint16_t));
  rangeMs = 1;
  if (!_historyCount) return 0;
  double mean = 0;
  for (int i = 0; i < _historyCount; i++) mean += _history[i];
  mean /= _historyCount;
  double largest = 0;
  for (int i = 0; i < _historyCount; i++) largest = fmax(largest, fabs(_history[i] - mean));
  static const float kRanges[] = {1, 2, 5, 10};
  rangeMs = 10;
  for (float range : kRanges) {
    if (largest / 1000.0 <= range) {
      rangeMs = range;
      break;
    }
  }
  for (int i = 0; i < _historyCount; i++) {
    double position = ((_history[i] - mean) / 1000.0 + rangeMs) / (2 * rangeMs) * kBins;
    int bin = position < 0 ? 0 : (position >= kBins ? kBins - 1 : (int)position);
    if (bins[bin] < 0xFFFF) bins[bin]++;
  }
  return _historyCount;
}

int ClockStats::beats(float *bpm, bool *dropout, int max) const
{
  int n = _beatCount < max ? _beatCount : max;
  int first = (_beatNext - n + kBeats) % kBeats;
  for (int i = 0; i < n; i++) {
    bpm[i] = _beatBpm[(first + i) % kBeats];
    if (dropout) dropout[i] = _beatDropout[(first + i) % kBeats];
  }
  return n;
}

// ---------------------------------------------------------------------------- phase

void ClockPhase::reset() { memset(this, 0, sizeof(*this)); }

void ClockPhase::onDinTick(uint32_t dinMicros, const ClockStats &usb, const ClockStats &din)
{
  float usbMean = usb.meanIntervalMicros(), dinMean = din.meanIntervalMicros();
  if (!usb.hasTicks() || usbMean <= 0 || dinMean <= 0) return;
  if (fabsf(usbMean - dinMean) > 0.01f * dinMean) return; // only for the same tempo (within 1%)
  int32_t phase = (int32_t)(dinMicros - kDinByteMicros - usb.lastTickMicros());
  if (fabsf(phase) > 4 * usbMean) return; // the USB clock has stopped
  while (phase > usbMean / 2) phase -= usbMean;
  while (phase < -usbMean / 2) phase += usbMean;

  _rolling[_next] = phase / 1000.0f;
  _next = (_next + 1) % kRolling;
  if (_count < kRolling) _count++;
  if (++_ticksInBeat >= 24) {
    _ticksInBeat = 0;
    _beatHistory[_historyNext] = meanMs();
    _historyNext = (_historyNext + 1) % kHistory;
    if (_historyCount < kHistory) _historyCount++;
  }
}

float ClockPhase::meanMs() const
{
  float sum = 0;
  for (int i = 0; i < _count; i++) sum += _rolling[i];
  return _count ? sum / _count : 0;
}

float ClockPhase::sdMs() const
{
  if (_count < 2) return 0;
  float mean = meanMs(), sum = 0;
  for (int i = 0; i < _count; i++) sum += (_rolling[i] - mean) * (_rolling[i] - mean);
  return sqrtf(sum / _count);
}

int ClockPhase::history(float *phaseMs, int max) const
{
  int n = _historyCount < max ? _historyCount : max;
  int first = (_historyNext - n + kHistory) % kHistory;
  for (int i = 0; i < n; i++) phaseMs[i] = _beatHistory[(first + i) % kHistory];
  return n;
}
