#include "usb_midi.h"

#include <SPI.h>
#include <usbh_midi.h>

static USB Usb;
static USBH_MIDI MidiDevice(&Usb);

bool UsbMidi::begin(MidiHandler handler)
{
  _handler = handler;
  return Usb.Init() != -1;
}

bool UsbMidi::connected() { return (bool)MidiDevice; }
uint16_t UsbMidi::vendorId() { return MidiDevice.idVendor(); }
uint16_t UsbMidi::productId() { return MidiDevice.idProduct(); }

void UsbMidi::poll()
{
  Usb.Task();
  if (!MidiDevice) {
    return;
  }
  uint8_t buf[MIDI_EVENT_PACKET_SIZE];
  uint16_t received = 0;
  // A few transfers per call keeps up with dense streams without starving the display
  for (int transfer = 0; transfer < 4; transfer++) {
    if (MidiDevice.RecvData(&received, buf) != 0 || received == 0) {
      return;
    }
    for (uint16_t i = 0; i + 4 <= received; i += 4) {
      if (buf[i] == 0 && buf[i + 1] == 0) {
        break; // padding: end of data
      }
      parsePacket(&buf[i]);
    }
  }
}

void UsbMidi::send(const MidiMessage &msg)
{
  if (!MidiDevice) {
    return;
  }
  // The library does not modify the buffer, it just lacks const in its API
  uint8_t *bytes = const_cast<uint8_t *>(msg.bytes);
  if (msg.isSysEx()) {
    MidiDevice.SendSysEx(bytes, msg.length, msg.cable);
  } else {
    MidiDevice.SendData(bytes, msg.cable);
  }
}

void UsbMidi::emit(uint8_t cable, const uint8_t *bytes, uint16_t length)
{
  MidiMessage msg = {MidiSource::Usb, cable, bytes, length};
  _handler(msg);
}

void UsbMidi::appendSysEx(const uint8_t *bytes, uint8_t length)
{
  if (bytes[0] == 0xF0) {
    _sysexLength = 0;
    _sysexOverflow = false;
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
void UsbMidi::parsePacket(const uint8_t *p)
{
  uint8_t cable = p[0] >> 4;
  uint8_t cin = p[0] & 0x0F;
  const uint8_t *midi = p + 1;

  switch (cin) {
    case 0x0: // reserved
    case 0x1: // reserved (cable events)
      break;
    case 0x4: // SysEx starts or continues
      appendSysEx(midi, 3);
      break;
    case 0x5: // single byte: SysEx end (F7) or system common (tune request)
      if (midi[0] == 0xF7) {
        appendSysEx(midi, 1);
        if (!_sysexOverflow) emit(cable, _sysex, _sysexLength);
        _sysexLength = 0;
      } else {
        emit(cable, midi, 1);
      }
      break;
    case 0x6: // SysEx ends with the following two bytes
    case 0x7: // SysEx ends with the following three bytes
      appendSysEx(midi, cin - 4);
      if (!_sysexOverflow) emit(cable, _sysex, _sysexLength);
      _sysexLength = 0;
      break;
    case 0x2: // two-byte system common
      emit(cable, midi, 2);
      break;
    case 0x3: // three-byte system common
      emit(cable, midi, 3);
      break;
    case 0xC: // program change
    case 0xD: // channel pressure
      emit(cable, midi, 2);
      break;
    case 0xF: // single byte (realtime)
      emit(cable, midi, 1);
      break;
    default: // 0x8-0xB, 0xE: note off/on, poly pressure, CC, pitch bend
      emit(cable, midi, 3);
      break;
  }
}
