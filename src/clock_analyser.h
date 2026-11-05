#pragma once

#include <atomic>
#include "clock_stats.h"
#include "midi_message.h"
#include "sync.h"

/*! Clock analyser (stage 9): statistics for the clock arriving on each input, fed by an
 *  InputHooks observer in the MIDI task (timestamps are the messages' timeMicros), read by the
 *  Analyser page through copies taken under a short critical section. */
class ClockAnalyser
{
public:
  /*! One CSV line: input,tick,time_us,interval_us,batched */
  struct CsvRecord
  {
    uint8_t input; ///< MidiSource
    uint32_t tick;
    uint32_t timeMicros;
    uint32_t interval;
    bool batched;
  };
  static constexpr size_t kCsvLength = 128;

  /*! InputHooks observer (MIDI task) */
  static void observer(const MidiMessage &msg, bool forwarded, void *ctx);
  void onMessage(const MidiMessage &msg, uint32_t nowMillis);

  // UI side
  void reset();
  /*! Copy one input's statistics (UI only; the copy lives here, not on the stack) */
  const ClockStats &copy(MidiSource input);
  ClockPhase phase();
  void setCsv(bool on) { _csv = on; }
  bool csv() const { return _csv; }
  bool popCsv(CsvRecord &record);

private:
  CriticalSection _lock;
  ClockStats _stats[2];
  ClockPhase _phase;
  ClockStats *_uiCopy = nullptr; ///< heap, not static: the Wi-Fi firmware needs the static RAM (stage 18)

  std::atomic<bool> _csv{false};
  CsvRecord _records[kCsvLength];
  std::atomic<size_t> _head{0}, _tail{0};
};
