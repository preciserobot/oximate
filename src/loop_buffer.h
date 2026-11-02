#pragma once

#include <stddef.h>
#include <stdint.h>

/*! One recorded channel message at 96 PPQN (4x MIDI clock), 8 bytes */
struct LoopEvent
{
  uint32_t tick; ///< position since the loop start, in 1/96 quarter notes
  uint8_t status, data1, data2;
  uint8_t flags; ///< bits 0-3: pass (overdub layer); bit 4: recorded from DIN (else USB)

  uint8_t pass() const { return flags & 0x0F; }
};

/*! Event storage of the looper (stage 13), plain C++ (host-tested).
 *  The loop is kept sorted by tick. A pass being recorded collects in a separate pending
 *  list and is merged at the end of each loop cycle (commit), so playback never sees a
 *  half-written loop. The memory is supplied by the owner (PSRAM on the device). */
class LoopBuffer
{
public:
  static constexpr uint32_t kPpqn = 96;
  static constexpr uint8_t kMaxPasses = 16;

  struct HeldNote
  {
    uint8_t status, note, velocity;
  };

  void attach(LoopEvent *events, uint32_t capacity, LoopEvent *pending, uint32_t pendingCapacity);
  void clear();
  void setLength(uint32_t ticks) { _length = ticks; }
  uint32_t length() const { return _length; }
  uint32_t count() const { return _count; }
  uint32_t capacity() const { return _capacity; }
  const LoopEvent &operator[](uint32_t i) const { return _events[i]; }
  const LoopEvent *events() const { return _events; }
  uint8_t passes() const; ///< highest pass + 1; 0 when empty
  uint32_t lowerBound(uint32_t tick) const; ///< first event with tick >= `tick`

  /*! Add to the pass being recorded; false if the pending list is full */
  bool record(uint32_t tick, uint8_t status, uint8_t data1, uint8_t data2, uint8_t pass, bool fromDin);
  uint32_t pendingCount() const { return _pendingCount; }
  void discardPending() { _pendingCount = 0; }

  /*! End of a cycle: note-ons without a note-off get one at the loop end (they are still held:
   *  the caller may restart them at tick 0 of the next cycle and gets them in `held`), note-ons
   *  move to the quantise grid (0 = off) with their note-off shifted by the same amount, and the
   *  pass is merged into the loop. Returns the number of held notes written to `held`. */
  int commit(uint32_t quantiseTicks, HeldNote *held, int maxHeld);

  /*! Remove the last pass; returns false when there is nothing to undo */
  bool undo();

  /*! Replace the loop with `count` events sorted by tick (loading a file) */
  bool load(const LoopEvent *events, uint32_t count, uint32_t length);

private:
  void sortPending();

  LoopEvent *_events = nullptr;
  uint32_t _capacity = 0;
  uint32_t _count = 0;
  LoopEvent *_pending = nullptr;
  uint32_t _pendingCapacity = 0;
  uint32_t _pendingCount = 0;
  uint32_t _length = 0;
};
