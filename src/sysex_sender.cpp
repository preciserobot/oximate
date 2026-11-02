#include "sysex_sender.h"

#include "din_midi.h"
#include "sysex_capture.h"
#include "usb_midi.h"

void SysExSender::begin(DinMidi &din, UsbMidi &usb, SysExCapture *capture)
{
  _din = &din;
  _usb = &usb;
  _capture = capture;
}

bool SysExSender::start(const uint8_t *data, const Syx::Message *messages, int count, uint8_t outputs,
                        uint16_t gapMillis)
{
  if (!data || !messages || count <= 0 || !outputs) return false;
  CriticalGuard guard(_lock);
  if (_request || _progress.state == State::Sending) return false;
  uint32_t total = 0;
  for (int i = 0; i < count; i++) total += messages[i].length;
  _data = data;
  _messages = messages;
  _count = count;
  _outputs = outputs;
  _gap = gapMillis;
  _request = true;
  _cancel = false;
  _progress = {State::Sending, 0, (uint32_t)count, 0, total};
  return true;
}

SysExSender::Progress SysExSender::progress() const
{
  CriticalGuard guard(_lock);
  return _progress;
}

void SysExSender::run(uint32_t nowMillis)
{
  {
    CriticalGuard guard(_lock);
    if (_request) {
      _request = false;
      _running = true;
      _index = 0;
      _offset = 0;
      _sent = 0;
      _resumeAt = 0;
      _total = _progress.totalBytes;
      _state = State::Sending;
    }
  }
  if (!_running) return;

  bool din = _outputs & kDin;
  bool usb = (_outputs & kUsb) && _usb->connected();
  if ((_outputs & kUsb) && !_usb->connected() && !din) {
    _state = State::NoOutput;
    _running = false;
  }
  if (_running && _capture) _capture->holdOff(nowMillis + kEchoHoldMillis);

  // Between messages: stop here when cancelled (after the last F7), else wait out the gap
  if (_running && _offset == 0) {
    if (_cancel) {
      _state = State::Cancelled;
      _running = false;
    } else if (_resumeAt && (int32_t)(nowMillis - _resumeAt) >= 0) {
      _resumeAt = 0;
    }
  }

  if (_running && !_resumeAt) {
    const Syx::Message &message = _messages[_index];
    size_t left = message.length - _offset;
    size_t room = left < kChunk ? left : kChunk;
    if (din && _din->txSpace() < room) room = _din->txSpace();
    if (usb) {
      size_t usbRoom = _usb->sysExPartRoom();
      if (usbRoom < room) room = usbRoom;
    }
    if (room > 0) {
      const uint8_t *bytes = _data + message.offset + _offset;
      if (din) _din->sendRaw(bytes, room);
      if (usb) _usb->sendSysExPart(bytes, room);
      _offset += room;
      _sent += room;
    }
    if (_offset >= message.length) { // F7 queued: next message after the gap
      _offset = 0;
      _index++;
      if (_index < _count && _gap) _resumeAt = (nowMillis + _gap) ? nowMillis + _gap : 1;
      if (_index >= _count) {
        _state = State::Done;
        _running = false;
      }
    }
  }

  CriticalGuard guard(_lock);
  _progress.state = _state;
  _progress.message = _index < _count ? _index : _count - 1;
  _progress.sentBytes = _sent;
}
