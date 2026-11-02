#pragma once

#include <stddef.h>
#include <stdint.h>

#include "loop_buffer.h"

/*! Standard MIDI File writing and reading for the looper (stage 13), plain C++ (host-tested).
 *  Loops are written at 96 PPQN (the looper's own resolution) with a tempo and a time
 *  signature; files of any resolution are read back rescaled to 96 PPQN. */
namespace Smf
{
struct Info
{
  float bpm = 120.0f;
  uint8_t beatsPerBar = 4;
  uint32_t lengthTicks = 0; ///< loop length at 96 PPQN (the end of track is placed there)
};

/*! Collects bytes; with no buffer it only counts (to size a file before writing it) */
struct Sink
{
  uint8_t *data = nullptr;
  size_t capacity = 0;
  size_t length = 0;
  bool overflow = false;

  void put(uint8_t b)
  {
    if (data) {
      if (length < capacity) data[length] = b;
      else overflow = true;
    }
    length++;
  }
};

/*! Type 0 (one track) or type 1 (a tempo track plus one track per channel). Returns the size
 *  in bytes; call with an empty Sink first to know how much to allocate. */
size_t write(const LoopEvent *events, uint32_t count, const Info &info, bool type1, Sink &sink);

/*! Read type 0 or 1 at any resolution: channel messages only (SysEx and other meta events are
 *  skipped), rescaled to 96 PPQN, sorted by tick. Tempo, time signature and the end of the
 *  longest track go into `info` (its length rounded up to whole bars). False if the data isn't
 *  a MIDI file or has more events than `capacity`. */
bool read(const uint8_t *data, size_t length, LoopEvent *events, uint32_t capacity, uint32_t &count, Info &info);
} // namespace Smf
