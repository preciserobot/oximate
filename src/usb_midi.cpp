#include "usb_midi.h"

#include <Arduino.h>
#include <SPI.h>
#include <usbh_midi.h>

#include "input_observers.h"
#include "spi_bus.h"

static USB Usb;
static USBH_MIDI MidiDevice(&Usb);

bool UsbMidi::begin(MidiHandler handler)
{
  _handler = handler;
  return Usb.Init() != -1;
}

void UsbMidi::poll()
{
  if (!SpiBus::tryLock()) {
    _counters.busBusy++;
    return; // the display is drawing; try again on the next poll
  }
  uint32_t now = micros();
  if (_lastPollMicros && now - _lastPollMicros > _counters.maxGapMicros) {
    _counters.maxGapMicros = now - _lastPollMicros;
  }
  _lastPollMicros = now;
  Usb.Task();
  bool connected = (bool)MidiDevice;
  if (connected != _connected) {
    _vid = connected ? MidiDevice.idVendor() : 0;
    _pid = connected ? MidiDevice.idProduct() : 0;
    _connected = connected;
    _txHead = _txTail = 0; // queued messages were meant for the previous device
  }
  if (connected) {
    flushTx();
    uint8_t buf[MIDI_EVENT_PACKET_SIZE];
    uint16_t received = 0;
    // A few transfers per poll keeps up with dense streams without holding the bus for long
    for (int transfer = 0; transfer < 4; transfer++) {
      if (MidiDevice.RecvData(&received, buf) != 0 || received == 0) {
        break;
      }
      now = micros();
      for (uint16_t i = 0; i + 4 <= received; i += 4) {
        if (buf[i] == 0 && buf[i + 1] == 0) {
          break; // padding: end of data
        }
        parsePacket(&buf[i], now);
      }
    }
  }
  SpiBus::unlock();
}

void UsbMidi::send(const MidiMessage &msg)
{
  if (!_connected) {
    return;
  }
  size_t used = (_txHead + kTxQueueLength - _txTail) % kTxQueueLength;
  if (kTxQueueLength - 1 - used < msg.length + 3u) {
    _counters.txDropped++;
    return;
  }
  auto push = [this](uint8_t b) {
    _tx[_txHead] = b;
    _txHead = (_txHead + 1) % kTxQueueLength;
  };
  push(msg.cable);
  push(msg.length & 0xFF);
  push(msg.length >> 8);
  for (uint16_t i = 0; i < msg.length; i++) {
    push(msg.bytes[i]);
  }
}

size_t UsbMidi::txSpace() const
{
  size_t used = (_txHead + kTxQueueLength - _txTail) % kTxQueueLength;
  return kTxQueueLength - 1 - used;
}

size_t UsbMidi::sysExPartRoom() const
{
  // Each record: 3 header bytes plus 4 packet bytes per 3 SysEx bytes (+1 packet of rounding)
  size_t space = txSpace();
  if (space < 3 + 8) return 0;
  size_t packets = (space - 3) / 4 - 1;
  size_t bytes = packets * 3;
  return bytes < kPacketRecordBytes ? bytes : kPacketRecordBytes;
}

void UsbMidi::sendSysExPart(const uint8_t *bytes, size_t length, uint8_t cable)
{
  if (!_connected || length == 0) return;
  uint8_t packets[(kPacketRecordBytes / 3 + 1) * 4];
  size_t used = 0;
  uint8_t packet[4];
  for (size_t i = 0; i < length && i < kPacketRecordBytes; i++) {
    if (_packer.push(bytes[i], cable, packet)) {
      memcpy(packets + used, packet, 4);
      used += 4;
    }
  }
  if (used == 0) return; // bytes wait in the packer for the next part
  size_t queued = (_txHead + kTxQueueLength - _txTail) % kTxQueueLength;
  if (kTxQueueLength - 1 - queued < used + 3) {
    _counters.txDropped++;
    return;
  }
  auto push = [this](uint8_t b) {
    _tx[_txHead] = b;
    _txHead = (_txHead + 1) % kTxQueueLength;
  };
  push(kPacketRecord);
  push(used & 0xFF);
  push(used >> 8);
  for (size_t i = 0; i < used; i++) push(packets[i]);
}

/*! Send queued messages; called from poll() while holding the SPI bus */
void UsbMidi::flushTx()
{
  auto pop = [this]() {
    uint8_t b = _tx[_txTail];
    _txTail = (_txTail + 1) % kTxQueueLength;
    return b;
  };
  // Bounded per poll so a long SysEx queue doesn't hold the bus for too long
  for (int sent = 0; sent < 16 && _txTail != _txHead; sent++) {
    uint8_t cable = pop();
    uint16_t length = pop();
    length |= pop() << 8;
    for (uint16_t i = 0; i < length; i++) {
      _txMessage[i] = pop();
    }
    if (cable == kPacketRecord) {
      MidiDevice.SendRawData(length, _txMessage); // streamed SysEx, already packed
    } else if (_txMessage[0] == 0xF0) {
      MidiDevice.SendSysEx(_txMessage, length, cable);
    } else {
      MidiDevice.SendData(_txMessage, cable);
    }
  }
}

void UsbMidi::emit(uint8_t cable, const uint8_t *bytes, uint16_t length, uint32_t timeMicros)
{
  MidiMessage msg = {MidiSource::Usb, cable, bytes, length, timeMicros};
  _handler(msg);
}

void UsbMidi::appendSysEx(const uint8_t *bytes, uint8_t length)
{
  if (bytes[0] == 0xF0) {
    _sysexLength = 0;
    _sysexOverflow = false;
    _sysexStart = _packetMicros;
  } else if (_sysexLength == 0) {
    _sysexOverflow = true; // continuation without a start: drop it
  }
  if (_sysexOverflow || _sysexLength + length > kMaxSysExLength) {
    _sysexOverflow = true;
    return;
  }
  memcpy(&_sysex[_sysexLength], bytes, length);
  _sysexLength += length;
}

/*! One 4-byte USB-MIDI event packet: [cable|CIN] [midi0] [midi1] [midi2] */
void UsbMidi::parsePacket(const uint8_t *p, uint32_t timeMicros)
{
  _packetMicros = timeMicros;
  uint8_t cable = p[0] >> 4;
  uint8_t cin = p[0] & 0x0F;
  const uint8_t *midi = p + 1;

  switch (cin) {
    case 0x0: // reserved
    case 0x1: // reserved (cable events)
      break;
    case 0x4: // SysEx starts or continues
      InputHooks::sysex(MidiSource::Usb, midi, 3); // dumps of any size, past the assembly limit
      appendSysEx(midi, 3);
      break;
    case 0x5: // single byte: SysEx end (F7) or system common (tune request)
      if (midi[0] == 0xF7) {
        InputHooks::sysex(MidiSource::Usb, midi, 1);
        appendSysEx(midi, 1);
        if (!_sysexOverflow) emit(cable, _sysex, _sysexLength, _sysexStart);
        _sysexLength = 0;
      } else {
        emit(cable, midi, 1, timeMicros);
      }
      break;
    case 0x6: // SysEx ends with the following two bytes
    case 0x7: // SysEx ends with the following three bytes
      InputHooks::sysex(MidiSource::Usb, midi, cin - 4);
      appendSysEx(midi, cin - 4);
      if (!_sysexOverflow) emit(cable, _sysex, _sysexLength, _sysexStart);
      _sysexLength = 0;
      break;
    case 0x2: // two-byte system common
      emit(cable, midi, 2, timeMicros);
      break;
    case 0x3: // three-byte system common
      emit(cable, midi, 3, timeMicros);
      break;
    case 0xC: // program change
    case 0xD: // channel pressure
      emit(cable, midi, 2, timeMicros);
      break;
    case 0xF: // single byte (realtime)
      emit(cable, midi, 1, timeMicros);
      break;
    default: // 0x8-0xB, 0xE: note off/on, poly pressure, CC, pitch bend
      emit(cable, midi, 3, timeMicros);
      break;
  }
}
