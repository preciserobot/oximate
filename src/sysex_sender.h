#pragma once

#include <atomic>
#include "midi_outputs.h"
#include "sync.h"
#include "syx_file.h"

class DinMidi;
class UsbMidi;
class SysExCapture;

/*! Streams SysEx messages of any length to DIN OUT and/or the USB device (stage 14).
 *  A job in the MIDI task, like the panic job: each loop it feeds as many bytes as the output
 *  queues take, with a gap between messages. Cancel (or panic) stops after the current F7, so no
 *  device is left inside an unterminated SysEx. UI side: start(), cancel(), progress(). */
class SysExSender
{
public:
  static constexpr uint8_t kDin = 1, kUsb = 2; ///< output bits
  static constexpr size_t kChunk = 256;         ///< most bytes per output per loop
  static constexpr uint32_t kEchoHoldMillis = 300;

  enum class State : uint8_t
  {
    Idle,
    Sending,
    Done,
    Cancelled,
    NoOutput, ///< USB chosen but no device connected
  };

  struct Progress
  {
    State state;
    uint32_t message;  ///< 0-based index of the message being sent
    uint32_t messages;
    uint32_t sentBytes;
    uint32_t totalBytes;
  };

  void begin(DinMidi &din, UsbMidi &usb, SysExCapture *capture = nullptr);

  // UI side
  /*! `data` and `messages` must stay valid until the job is no longer Sending */
  bool start(const uint8_t *data, const Syx::Message *messages, int count, uint8_t outputs, uint16_t gapMillis);
  void cancel() { _cancel = true; }
  Progress progress() const;
  bool busy() const { return progress().state == State::Sending; }

  // MIDI task side
  void run(uint32_t nowMillis);

private:
  DinMidi *_din = nullptr;
  UsbMidi *_usb = nullptr;
  SysExCapture *_capture = nullptr;

  // Handoff (guarded by _lock)
  mutable CriticalSection _lock;
  bool _request = false;
  const uint8_t *_data = nullptr;
  const Syx::Message *_messages = nullptr;
  int _count = 0;
  uint8_t _outputs = 0;
  uint16_t _gap = 0;
  Progress _progress = {State::Idle, 0, 0, 0, 0};
  std::atomic<bool> _cancel{false};

  // MIDI task state
  bool _running = false;
  int _index = 0;
  uint32_t _offset = 0;      // within the current message
  uint32_t _sent = 0;
  uint32_t _total = 0;
  uint32_t _resumeAt = 0;    // millis: end of the gap, 0 = no gap running
  State _state = State::Idle;
};
