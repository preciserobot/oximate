#include "clock_analyser.h"

#include <Arduino.h>
#include <string.h>

void ClockAnalyser::observer(const MidiMessage &msg, bool, void *ctx)
{
  uint8_t status = msg.status();
  if (status == 0xF8 || status == 0xFA || status == 0xFB || status == 0xFC || status == 0xF2) {
    static_cast<ClockAnalyser *>(ctx)->onMessage(msg, millis());
  }
}

void ClockAnalyser::onMessage(const MidiMessage &msg, uint32_t nowMillis)
{
  int input = (int)msg.source;
  uint32_t previous = 0;
  uint32_t ticks = 0;
  {
    CriticalGuard guard(_lock);
    ClockStats &stats = _stats[input];
    previous = stats.lastTickMicros();
    stats.onMessage(msg.bytes, msg.length, msg.timeMicros, nowMillis);
    ticks = stats.ticks();
    if (msg.status() == 0xF8 && msg.source == MidiSource::Din) {
      _phase.onDinTick(msg.timeMicros, _stats[(int)MidiSource::Usb], stats);
    }
  }
  if (_csv && msg.status() == 0xF8) {
    size_t head = _head.load(std::memory_order_relaxed);
    size_t next = (head + 1) % kCsvLength;
    if (next != _tail.load(std::memory_order_acquire)) { // full: the line is dropped
      uint32_t interval = ticks > 1 ? msg.timeMicros - previous : 0;
      _records[head] = {(uint8_t)input, ticks, msg.timeMicros, interval, ticks > 1 && interval < ClockStats::kBatchedMicros};
      _head.store(next, std::memory_order_release);
    }
  }
}

void ClockAnalyser::reset()
{
  CriticalGuard guard(_lock);
  _stats[0].reset();
  _stats[1].reset();
  _phase.reset();
}

const ClockStats &ClockAnalyser::copy(MidiSource input)
{
  CriticalGuard guard(_lock);
  memcpy(&_uiCopy, &_stats[(int)input], sizeof(ClockStats));
  return _uiCopy;
}

ClockPhase ClockAnalyser::phase()
{
  CriticalGuard guard(_lock);
  return _phase;
}

bool ClockAnalyser::popCsv(CsvRecord &record)
{
  size_t tail = _tail.load(std::memory_order_relaxed);
  if (tail == _head.load(std::memory_order_acquire)) return false;
  record = _records[tail];
  _tail.store((tail + 1) % kCsvLength, std::memory_order_release);
  return true;
}
