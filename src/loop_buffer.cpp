#include "loop_buffer.h"

#include <string.h>

static bool isNoteOn(const LoopEvent &e) { return (e.status & 0xF0) == 0x90 && e.data2 > 0; }
static bool isNoteOff(const LoopEvent &e) { return (e.status & 0xF0) == 0x80 || ((e.status & 0xF0) == 0x90 && e.data2 == 0); }
static bool sameNote(const LoopEvent &a, const LoopEvent &b)
{
  return (a.status & 0x0F) == (b.status & 0x0F) && a.data1 == b.data1;
}

void LoopBuffer::attach(LoopEvent *events, uint32_t capacity, LoopEvent *pending, uint32_t pendingCapacity)
{
  _events = events;
  _capacity = capacity;
  _pending = pending;
  _pendingCapacity = pendingCapacity;
  clear();
}

void LoopBuffer::clear()
{
  _count = 0;
  _pendingCount = 0;
}

uint8_t LoopBuffer::passes() const
{
  uint8_t highest = 0;
  for (uint32_t i = 0; i < _count; i++) {
    if (_events[i].pass() + 1 > highest) highest = _events[i].pass() + 1;
  }
  return highest;
}

uint32_t LoopBuffer::lowerBound(uint32_t tick) const
{
  uint32_t low = 0, high = _count;
  while (low < high) {
    uint32_t middle = (low + high) / 2;
    if (_events[middle].tick < tick) low = middle + 1;
    else high = middle;
  }
  return low;
}

bool LoopBuffer::record(uint32_t tick, uint8_t status, uint8_t data1, uint8_t data2, uint8_t pass, bool fromDin)
{
  if (!_pending || _pendingCount >= _pendingCapacity || !_length) return false;
  _pending[_pendingCount++] = {tick % _length, status, data1, data2, (uint8_t)((pass & 0x0F) | (fromDin ? 0x10 : 0))};
  return true;
}

/*! Insertion sort, stable: a pass is recorded in time order, so this is close to linear */
void LoopBuffer::sortPending()
{
  for (uint32_t i = 1; i < _pendingCount; i++) {
    LoopEvent e = _pending[i];
    uint32_t j = i;
    while (j > 0 && _pending[j - 1].tick > e.tick) {
      _pending[j] = _pending[j - 1];
      j--;
    }
    _pending[j] = e;
  }
}

int LoopBuffer::commit(uint32_t quantiseTicks, HeldNote *held, int maxHeld)
{
  int heldCount = 0;
  if (!_length) {
    _pendingCount = 0;
    return 0;
  }
  // Pair note-ons with their note-off (the next one of the same note); quantise moves both
  for (uint32_t i = 0; i < _pendingCount; i++) {
    LoopEvent &on = _pending[i];
    if (!isNoteOn(on)) continue;
    int32_t off = -1;
    for (uint32_t j = i + 1; j < _pendingCount; j++) {
      if (isNoteOff(_pending[j]) && sameNote(_pending[j], on) && _pending[j].pass() == on.pass()) {
        off = (int32_t)j;
        break;
      }
      if (isNoteOn(_pending[j]) && sameNote(_pending[j], on)) break; // retriggered without an off
    }
    if (off < 0) {
      // Still held at the loop end: cut it there; the caller may restart it at tick 0
      if (_pendingCount < _pendingCapacity) {
        _pending[_pendingCount++] = {_length - 1, (uint8_t)(0x80 | (on.status & 0x0F)), on.data1, 0, on.flags};
        off = (int32_t)_pendingCount - 1;
      }
      if (held && heldCount < maxHeld) held[heldCount++] = {on.status, on.data1, on.data2};
    }
    if (quantiseTicks) {
      uint32_t grid = ((on.tick + quantiseTicks / 2) / quantiseTicks) * quantiseTicks;
      int32_t delta = (int32_t)grid - (int32_t)on.tick;
      on.tick = grid % _length;
      if (off >= 0) {
        int32_t moved = (int32_t)_pending[off].tick + delta;
        if (moved < (int32_t)on.tick) moved = on.tick; // the off never comes before the on
        _pending[off].tick = (uint32_t)moved % _length;
      }
    }
  }
  sortPending();

  // Merge the sorted pass into the sorted loop, from the back, in place
  uint32_t room = _capacity - _count;
  uint32_t add = _pendingCount < room ? _pendingCount : room;
  int64_t i = (int64_t)_count - 1, j = (int64_t)add - 1, k = (int64_t)(_count + add) - 1;
  while (j >= 0) {
    // At equal ticks the existing events come first: the new pass is played after them
    if (i >= 0 && _events[i].tick > _pending[j].tick) _events[k--] = _events[i--];
    else _events[k--] = _pending[j--];
  }
  _count += add;
  _pendingCount = 0;
  return heldCount;
}

bool LoopBuffer::undo()
{
  uint8_t passCount = passes();
  if (!passCount) return false;
  uint8_t last = passCount - 1;
  uint32_t kept = 0;
  for (uint32_t i = 0; i < _count; i++) {
    if (_events[i].pass() != last) _events[kept++] = _events[i];
  }
  _count = kept;
  return true;
}

bool LoopBuffer::load(const LoopEvent *events, uint32_t count, uint32_t length)
{
  if (count > _capacity || !length) return false;
  memcpy(_events, events, count * sizeof(LoopEvent));
  _count = count;
  _length = length;
  _pendingCount = 0;
  return true;
}
