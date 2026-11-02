#include "gen_core.h"

#include <math.h>

#include "music_theory.h"

void TuringRegister::reseed(uint32_t seed)
{
  _rng.seed(seed);
  for (uint8_t &value : _values) value = _rng.next() & 0xFF;
  _position = 0;
}

void TuringRegister::setLength(int length)
{
  _length = length < 2 ? 2 : (length > kMaxLength ? kMaxLength : length);
  if (_position >= _length) _position = 0;
}

uint8_t TuringRegister::step(int mutatePercent)
{
  if (mutatePercent > 0 && _rng.chance(mutatePercent)) {
    _values[_position] = _rng.next() & 0xFF;
  }
  uint8_t value = _values[_position];
  _position = (_position + 1) % _length;
  return value;
}

int turingNote(uint8_t value, int densityPercent, uint8_t rootPc, uint16_t scaleMask, int baseOctave,
               int rangeOctaves)
{
  // The rhythm comes from the same value: a different "digit" of it than the pitch uses
  if ((value * 7 + 3) % 100 >= densityPercent) return -1;
  if (rangeOctaves < 1) rangeOctaves = 1;
  int span = rangeOctaves * 12;
  int note = (baseOctave + 1) * 12 + value * span / 256;
  if (note > 127) note = 127;
  return MusicTheory::quantise(note, rootPc, scaleMask);
}

uint8_t modValue(ModShape shape, float phase, float from, float to, uint8_t min, uint8_t max)
{
  float x; // 0..1
  switch (shape) {
    case ModShape::SampleHold:   x = from; break;
    case ModShape::SmoothRandom: x = from + (to - from) * (0.5f - 0.5f * cosf(phase * (float)M_PI)); break;
    case ModShape::Sine:         x = 0.5f - 0.5f * cosf(phase * 2 * (float)M_PI); break; // starts at min
    case ModShape::Triangle:     x = phase < 0.5f ? phase * 2 : 2 - phase * 2; break;
    case ModShape::RampUp:       x = phase; break;
    case ModShape::RampDown:     x = 1 - phase; break;
    default:                     x = phase < 0.5f ? 1 : 0; break; // square
  }
  if (x < 0) x = 0;
  if (x > 1) x = 1;
  return (uint8_t)lroundf(min + (max - min) * x);
}
