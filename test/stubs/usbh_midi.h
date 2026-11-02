#pragma once
// Host stand-in for USB Host Shield 2.0's USBH_MIDI: a device that is always connected,
// receives nothing, and records what is sent
#include <stdint.h>
#include <string.h>
#include <vector>
#define MIDI_EVENT_PACKET_SIZE 64
inline std::vector<std::vector<uint8_t>> gUsbSent; // everything sent to the fake device
struct USB
{
  int Init() { return 0; }
  void Task() {}
};
struct USBH_MIDI
{
  USBH_MIDI(USB *) {}
  operator bool() { return true; }
  uint16_t idVendor() { return 0x1234; }
  uint16_t idProduct() { return 0x5678; }
  uint8_t RecvData(uint16_t *, uint8_t *) { return 1; }
  uint8_t SendData(uint8_t *p, uint8_t) { gUsbSent.push_back({p, p + 3}); return 0; }
  uint8_t SendSysEx(uint8_t *p, uint16_t n, uint8_t) { gUsbSent.push_back({p, p + n}); return 0; }
  uint8_t SendRawData(uint16_t n, uint8_t *p) { gUsbSent.push_back({p, p + n}); return 0; }
};
