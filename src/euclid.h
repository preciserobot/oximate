#pragma once

#include <stdint.h>

/*! Euclidean rhythm: `hits` spread as evenly as possible over `steps` (1-32), moved later by
 *  `rotation` steps. Bit i of the result = step i is a hit. Bresenham form (step i hits when
 *  (i * hits) % steps < hits): the same patterns as Bjorklund's algorithm up to rotation,
 *  e.g. E(3,8) = x..x..x. Shared by the drum machine's Euclid fill and the Gen page. */
inline uint32_t euclid(int steps, int hits, int rotation)
{
  if (steps < 1) return 0;
  if (steps > 32) steps = 32;
  if (hits <= 0) return 0;
  if (hits > steps) hits = steps;
  rotation = ((rotation % steps) + steps) % steps;
  uint32_t mask = 0;
  for (int i = 0; i < steps; i++) {
    if ((i * hits) % steps < hits) mask |= 1u << ((i + rotation) % steps);
  }
  return mask;
}
