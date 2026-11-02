#include "sysex_capture.h"

#include <Arduino.h>

#include "input_observers.h"

void SysExCapture::begin(uint8_t *buffer, size_t capacity)
{
  _buffer = buffer;
  _capacity = buffer ? capacity : 0;
}

void SysExCapture::registerHooks()
{
  InputHooks::setSysExTap(tapHook, this);
  InputHooks::addInterceptor(interceptHook, this);
}

void SysExCapture::start(uint8_t inputs, bool captureOnly)
{
  if (!_buffer) return;
  _active = false; // the MIDI task stops appending before the counters are reset
  _length.store(0, std::memory_order_release);
  _messages = 0;
  _lastByte = 0;
  _full = false;
  _inMessage = false;
  _inputs = inputs;
  _captureOnly = captureOnly;
  _active = true;
}

void SysExCapture::clear()
{
  _active = false;
  _length.store(0, std::memory_order_release);
  _messages = 0;
  _lastByte = 0;
  _full = false;
}

SysExCapture::Status SysExCapture::status() const
{
  return {_active, _full, length(), _messages, _lastByte};
}

bool SysExCapture::poll(uint32_t nowMillis, uint32_t idleMillis)
{
  uint32_t last = _lastByte;
  if (_active && last && nowMillis - last >= idleMillis) {
    _active = false;
    return true;
  }
  return false;
}

void SysExCapture::tap(MidiSource source, const uint8_t *bytes, uint16_t length, uint32_t nowMillis)
{
  uint8_t bit = source == MidiSource::Usb ? kUsb : kDin;
  if (!_active || !(_inputs & bit)) return;
  uint32_t holdOff = _holdOffUntil;
  if (holdOff && (int32_t)(nowMillis - holdOff) < 0) return; // our own send echoed back
  uint32_t at = _length.load(std::memory_order_relaxed);
  for (uint16_t i = 0; i < length; i++) {
    uint8_t b = bytes[i];
    if (b == 0xF0) {
      _inMessage = true;
    } else if (!_inMessage) {
      continue; // joined in the middle of a message: wait for the next F0
    }
    if (at >= _capacity) {
      _full = true;
      _active = false;
      break;
    }
    _buffer[at++] = b;
    if (b == 0xF7) {
      _inMessage = false;
      _messages++;
    }
  }
  _length.store(at, std::memory_order_release);
  _lastByte = nowMillis ? nowMillis : 1;
}

bool SysExCapture::intercept(const MidiMessage &msg) const
{
  if (!_active || !_captureOnly || !msg.isSysEx()) return false;
  uint8_t bit = msg.source == MidiSource::Usb ? kUsb : kDin;
  return (_inputs & bit) != 0;
}

void SysExCapture::tapHook(MidiSource source, const uint8_t *bytes, uint16_t length, void *ctx)
{
  static_cast<SysExCapture *>(ctx)->tap(source, bytes, length, millis());
}

bool SysExCapture::interceptHook(const MidiMessage &msg, void *ctx)
{
  return static_cast<SysExCapture *>(ctx)->intercept(msg);
}
