#pragma once

#include "midi_message.h"

class DinMidi;
class UsbMidi;

/*! Which input's clock and transport (F8, FA, FB, FC, F2) is forwarded */
enum class ClockSource : uint8_t
{
  Both = 0,
  Usb = 1,
  Din = 2,
  Off = 3,
};

/*! Routing settings, persisted in NVS. Bump kVersion when the layout changes. */
struct RouterConfig
{
  static constexpr uint8_t kVersion = 1;

  uint8_t version = kVersion;
  bool usbToDin = true;                     ///< USB device -> DIN OUT (+ SAM2695)
  bool dinToDin = true;                     ///< DIN IN -> DIN OUT (software thru)
  bool dinToUsb = true;                     ///< DIN IN -> USB device
  uint16_t channelMask[2] = {0xFFFF, 0xFFFF}; ///< per input (MidiSource), bit n = channel n+1 passes
  uint8_t forceChannel[2] = {0, 0};         ///< per input: 0 = keep, 1..16 = rewrite to this channel
  ClockSource clock = ClockSource::Both;
  bool sysex = true;
  uint8_t ledLevel = 2;                     ///< 0 off, 1 low, 2 medium, 3 high
};

/*! Filters, remaps and forwards merged MIDI according to RouterConfig */
class Router
{
public:
  void begin(DinMidi &din, UsbMidi &usb);

  /*! Forward a message; returns false if it was filtered out completely */
  bool route(const MidiMessage &msg);

  RouterConfig &config() { return _config; }
  void changed();       ///< call after modifying config(); saved after a short delay
  void resetDefaults();
  void poll();          ///< performs delayed saves

private:
  bool passes(const MidiMessage &msg) const;
  void load();
  void save();

  RouterConfig _config;
  DinMidi *_din = nullptr;
  UsbMidi *_usb = nullptr;
  uint32_t _changedMillis = 0;
  bool _dirty = false;
};
