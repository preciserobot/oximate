#pragma once

#include <stdint.h>
#include "sync.h"

/*! Cues you can feel (vibration) or hear (a click), from docs/stages/stage-10.md.
 *  Any stage posts cues with Cues::post(); the haptics player (stage 10) takes them on the UI
 *  thread, applies priorities and the user's settings, and drives the motor and speaker.
 *  Shared before stages 10 and 22 were built, so both use the same list. */
enum class Cue : uint8_t
{
  Beat,
  Downbeat,
  CountIn,
  CountInLast,
  PhraseStart,
  PhraseEnding,
  ClockLost,
  ClockLocked,
  TouchTick,
  PanicDone,
  Kick,
  RightAnswer,
  WrongAnswer,
  LowBattery,
  Count,
};

namespace Cues
{
struct Item
{
  Cue cue;
  uint8_t velocity;   ///< for Kick (pattern length scales with it), else 0
  uint32_t dueMicros; ///< micros() when it should be felt; 0 = now
};

/*! Small queue, safe to post to from any task (MIDI task and UI); taken on the UI thread */
class Queue
{
public:
  static constexpr int kLength = 32;

  bool post(Cue cue, uint32_t dueMicros = 0, uint8_t velocity = 0)
  {
    CriticalGuard guard(_lock);
    int next = (_head + 1) % kLength;
    if (next == _tail) {
      _dropped++;
      return false;
    }
    _items[_head] = {cue, velocity, dueMicros};
    _head = next;
    return true;
  }

  bool take(Item &item)
  {
    CriticalGuard guard(_lock);
    if (_tail == _head) return false;
    item = _items[_tail];
    _tail = (_tail + 1) % kLength;
    return true;
  }

  uint32_t dropped() const { return _dropped; }

private:
  CriticalSection _lock;
  Item _items[kLength];
  int _head = 0, _tail = 0;
  uint32_t _dropped = 0;
};

inline Queue &queue()
{
  static Queue q;
  return q;
}

/*! Post a cue from any task. Without the haptics player (stage 10) nothing consumes them. */
inline bool post(Cue cue, uint32_t dueMicros = 0, uint8_t velocity = 0)
{
  return queue().post(cue, dueMicros, velocity);
}
} // namespace Cues
