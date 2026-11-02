#pragma once

#include <stdint.h>
#include "cue_queue.h"

/*! Vibration patterns for each cue (docs/stages/stage-10.md "Patterns"). Pure data.
 *  A pattern is a list of (level, milliseconds) segments; level 0 = motor off. Levels are
 *  M5Unified setVibration() values at full strength (235 is the maximum useful level on the
 *  Core2) and are scaled by the strength setting. */
namespace HapticPatterns
{
struct Segment
{
  uint8_t level;
  uint16_t millis;
};

struct Pattern
{
  const Segment *segments;
  uint8_t count;
  uint8_t priority; ///< a higher one interrupts a lower one; a lower one is dropped while a higher plays
};

static const Segment kBeat[] = {{235, 70}};
static const Segment kDownbeat[] = {{235, 140}};
static const Segment kPhraseStart[] = {{235, 140}, {0, 90}, {235, 140}};
static const Segment kTriple[] = {{235, 60}, {0, 80}, {235, 60}, {0, 80}, {235, 60}};
static const Segment kLost[] = {{235, 400}};
static const Segment kLocked[] = {{180, 60}, {0, 80}, {235, 60}};
static const Segment kTouch[] = {{200, 25}};
static const Segment kPanic[] = {{235, 250}};
static const Segment kKick[] = {{235, 60}}; // length replaced by the velocity (40-90 ms)
static const Segment kRight[] = {{235, 60}};
static const Segment kBattery[] = {{160, 200}, {0, 200}, {160, 200}};

#define HAPTIC_PATTERN(segments, priority) {segments, sizeof(segments) / sizeof(segments[0]), priority}

/*! Indexed by Cue. Priorities: clock lost / panic > phrase > downbeat > beat / count-in > kick >
 *  answer > battery > touch tick */
static const Pattern kPatterns[(int)Cue::Count] = {
    HAPTIC_PATTERN(kBeat, 5),        // Beat
    HAPTIC_PATTERN(kDownbeat, 6),    // Downbeat
    HAPTIC_PATTERN(kBeat, 5),        // CountIn
    HAPTIC_PATTERN(kDownbeat, 5),    // CountInLast
    HAPTIC_PATTERN(kPhraseStart, 7), // PhraseStart
    HAPTIC_PATTERN(kTriple, 7),      // PhraseEnding
    HAPTIC_PATTERN(kLost, 8),        // ClockLost
    HAPTIC_PATTERN(kLocked, 6),      // ClockLocked
    HAPTIC_PATTERN(kTouch, 1),       // TouchTick
    HAPTIC_PATTERN(kPanic, 8),       // PanicDone
    HAPTIC_PATTERN(kKick, 4),        // Kick
    HAPTIC_PATTERN(kRight, 3),       // RightAnswer
    HAPTIC_PATTERN(kTriple, 3),      // WrongAnswer
    HAPTIC_PATTERN(kBattery, 2),     // LowBattery
};

#undef HAPTIC_PATTERN

/*! Kick pulse length from its velocity: 40 ms (soft) to 90 ms (hard) */
inline uint16_t kickMillis(uint8_t velocity) { return 40 + (velocity > 127 ? 127 : velocity) * 50 / 127; }
} // namespace HapticPatterns
