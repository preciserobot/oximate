#pragma once

#include <stdint.h>

/*! Pure building blocks of the stage 12 generators (no Arduino, host-tested): a seeded random
 *  generator, the Turing register and the mod-lane shapes. Euclid lives in euclid.h. */

/*! xorshift32: small, fast, and the same sequence for the same seed (tests, stored seeds) */
struct XorShift32
{
  uint32_t state = 2463534242u;

  void seed(uint32_t value) { state = value ? value : 2463534242u; }
  uint32_t next()
  {
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
  }
  /*! True with `percent` % probability */
  bool chance(int percent) { return percent >= 100 || (int)(next() % 100) < percent; }
};

/*! Looping register of up to 16 values (0-255), after the "Turing Machine" module: each step
 *  outputs the head value and moves on; a value coming round is replaced by a new random one
 *  with the mutation probability. Mutation 0 repeats exactly every `length` steps. */
class TuringRegister
{
public:
  static constexpr int kMaxLength = 16;

  void reseed(uint32_t seed);
  void setLength(int length);
  int length() const { return _length; }
  /*! Next value (mutating the slot first with `mutatePercent` % chance) */
  uint8_t step(int mutatePercent);
  uint8_t value(int index) const { return _values[index % kMaxLength]; }
  int position() const { return _position; } ///< index of the next value
  void restart() { _position = 0; }

private:
  uint8_t _values[kMaxLength] = {};
  int _length = 8;
  int _position = 0;
  XorShift32 _rng;
};

/*! A Turing value turned into a note: scaled over `rangeOctaves` above C of `baseOctave`
 *  (C4 = 60, octave 4), then quantised to the scale. Returns -1 for a rest (density: values whose
 *  low part is >= `densityPercent` rest; 100 never rests, 0 always does). */
int turingNote(uint8_t value, int densityPercent, uint8_t rootPc, uint16_t scaleMask, int baseOctave,
               int rangeOctaves);

enum class ModShape : uint8_t
{
  SampleHold,
  SmoothRandom,
  Sine,
  Triangle,
  RampUp,
  RampDown,
  Square,
  Count,
};

/*! Value of a mod lane: `phase` 0..1 within the cycle; `from` and `to` are the cycle's random
 *  start and end (0-1, for the random shapes); scaled to min..max (max < min inverts) */
uint8_t modValue(ModShape shape, float phase, float from, float to, uint8_t min, uint8_t max);
