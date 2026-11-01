#pragma once

#include <math.h>
#include <stdint.h>

/*! Tap tempo: averages the last 4 intervals. A gap over 2 s starts again; taps more than
 *  30% away from the average are ignored. Returns a tempo from the second tap on. */
class TapTempo
{
public:
  static constexpr uint32_t kResetMicros = 2000000;
  static constexpr int kIntervals = 4;

  /*! Register a tap at `micros`; returns the new BPM (20-300), or 0 if there is none yet */
  float tap(uint32_t micros)
  {
    if (_count > 0) {
      uint32_t interval = micros - _last;
      if (interval > kResetMicros) {
        _count = 0;
      } else {
        float average = this->average();
        if (_count > 1 && fabsf(interval - average) > 0.3f * average) {
          return 0; // stray tap: ignore it, but keep the timing reference
        }
        _intervals[_next] = interval;
        _next = (_next + 1) % kIntervals;
        if (_count <= kIntervals) _count++;
      }
    }
    if (_count == 0) _count = 1;
    _last = micros;
    if (_count < 2) {
      return 0;
    }
    float bpm = 60e6f / average();
    return bpm < 20 ? 20 : (bpm > 300 ? 300 : bpm);
  }

private:
  float average() const
  {
    int n = _count - 1 < kIntervals ? _count - 1 : kIntervals;
    if (n <= 0) return 0;
    float sum = 0;
    for (int i = 0; i < n; i++) sum += _intervals[(_next - 1 - i + kIntervals) % kIntervals];
    return sum / n;
  }

  uint32_t _intervals[kIntervals] = {0};
  int _next = 0;
  int _count = 0; // taps seen in the current run (intervals = count - 1)
  uint32_t _last = 0;
};
