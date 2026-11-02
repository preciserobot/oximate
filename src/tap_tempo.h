#pragma once

#include <math.h>
#include <stdint.h>

/*! Tap tempo: averages the last 4 intervals and only gives a tempo from the 4th tap on, so one
 *  sloppy tap can't set it. A gap of about two intervals counts as a missed tap and is split in
 *  two; other taps more than 30% off the average are ignored; a gap over 3 s starts again.
 *  (The touch buttons and screen are small, so taps get missed or land late.) */
class TapTempo
{
public:
  static constexpr uint32_t kResetMicros = 3000000;
  static constexpr int kIntervals = 4;
  static constexpr int kMinTaps = 4;        ///< taps before the tempo is set
  static constexpr float kStrayRatio = 0.3f; ///< ignore taps further off the average than this
  static constexpr float kMissedRatio = 0.2f; ///< a gap of 2 x average +-20% is a missed tap

  /*! Register a tap at `micros`; returns the new BPM (20-300), or 0 while there is none yet */
  float tap(uint32_t micros)
  {
    if (_taps > 0) {
      uint32_t interval = micros - _last;
      if (interval > kResetMicros) {
        _taps = 0;
        _count = 0;
      } else if (_count >= 1) {
        float average = this->average();
        if (fabsf(interval - 2 * average) <= kMissedRatio * 2 * average) {
          add(interval / 2); // one tap didn't register: fill it in
          add(interval - interval / 2);
          _taps++;
        } else if (_count >= 2 && fabsf(interval - average) > kStrayRatio * average) {
          return 0; // stray tap: ignore it, but keep the timing reference
        } else {
          add(interval);
        }
      } else {
        add(interval);
      }
    }
    _taps++;
    _last = micros;
    if (_taps < kMinTaps || _count == 0) {
      return 0;
    }
    float bpm = 60e6f / average();
    return bpm < 20 ? 20 : (bpm > 300 ? 300 : bpm);
  }

  /*! Taps counted in the current run (including filled-in ones), for "Tap 2/4" feedback */
  int taps() const { return _taps; }
  /*! True while a run is going on (the last tap was less than kResetMicros ago) */
  bool active(uint32_t micros) const { return _taps > 0 && micros - _last <= kResetMicros; }

private:
  void add(uint32_t interval)
  {
    _intervals[_next] = interval;
    _next = (_next + 1) % kIntervals;
    if (_count < kIntervals) _count++;
  }

  float average() const
  {
    if (_count == 0) return 0;
    float sum = 0;
    for (int i = 0; i < _count; i++) sum += _intervals[(_next - 1 - i + kIntervals) % kIntervals];
    return sum / _count;
  }

  uint32_t _intervals[kIntervals] = {0};
  int _next = 0;
  int _count = 0; // intervals stored (max kIntervals)
  int _taps = 0;  // taps in the current run
  uint32_t _last = 0;
};
