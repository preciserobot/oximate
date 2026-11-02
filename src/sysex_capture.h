#pragma once

#include <atomic>
#include "midi_message.h"

/*! Captures SysEx dumps from DIN and/or USB into a buffer (PSRAM on the device), for the SysEx
 *  librarian (stage 14). Bytes arrive through the InputHooks SysEx tap in the MIDI task, so dumps
 *  of any size are captured byte-exact, past the parsers' 1024-byte assembly limit.
 *  UI side: start(), stop(), clear(), status(), data(); poll() ends a capture after it went idle.
 *  MIDI task side: tap() (bytes) and intercept() ("capture only": the router doesn't forward
 *  SysEx from a capturing input, so a dump neither floods DIN OUT nor loops back). */
class SysExCapture
{
public:
  static constexpr uint8_t kUsb = 1, kDin = 2; ///< input bits

  struct Status
  {
    bool active;   ///< capturing
    bool full;     ///< stopped because the buffer was full
    uint32_t bytes;
    uint32_t messages; ///< complete messages (F7 seen)
    uint32_t lastByteMillis; ///< 0 = nothing yet
  };

  void begin(uint8_t *buffer, size_t capacity);
  bool ready() const { return _buffer != nullptr; }
  void registerHooks(); ///< SysEx tap and capture-only interceptor; call in setup()

  // UI side
  void start(uint8_t inputs, bool captureOnly);
  void stop() { _active = false; }
  void clear();
  Status status() const;
  const uint8_t *data() const { return _buffer; }
  uint32_t length() const { return _length.load(std::memory_order_acquire); }
  /*! Ends an active capture `idleMillis` after its last byte; true when it just ended */
  bool poll(uint32_t nowMillis, uint32_t idleMillis);

  // MIDI task side
  void tap(MidiSource source, const uint8_t *bytes, uint16_t length, uint32_t nowMillis);
  bool intercept(const MidiMessage &msg) const;
  /*! While sending (and briefly after), bytes are ignored: a device echoing our own SysEx (MIDI
   *  thru) must not end up in the capture */
  void holdOff(uint32_t untilMillis) { _holdOffUntil = untilMillis; }

  static void tapHook(MidiSource source, const uint8_t *bytes, uint16_t length, void *ctx);
  static bool interceptHook(const MidiMessage &msg, void *ctx);

private:
  uint8_t *_buffer = nullptr;
  size_t _capacity = 0;
  std::atomic<uint32_t> _length{0};
  std::atomic<uint32_t> _messages{0};
  std::atomic<uint32_t> _lastByte{0};
  std::atomic<bool> _active{false};
  std::atomic<bool> _full{false};
  std::atomic<uint8_t> _inputs{0};
  std::atomic<bool> _captureOnly{true};
  std::atomic<uint32_t> _holdOffUntil{0};
  bool _inMessage = false; // MIDI task: inside an F0 ... F7
};
