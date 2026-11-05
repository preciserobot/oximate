#pragma once

#include "midi_message.h"
#include "midi_outputs.h"

/*! Which radio runs (stages 17 and 18). Only one at a time: switching reboots, because the BLE and
 *  Wi-Fi stacks together would take most of the internal RAM (decided 2026-11-05). */
enum class RadioMode : uint8_t
{
  Off = 0,
  Ble = 1,  ///< BLE MIDI (stage 17)
  Wifi = 2, ///< RTP-MIDI / AppleMIDI over Wi-Fi (stage 18)
};

/*! Radio settings, persisted in NVS (key "radio"). Bump kVersion when the layout changes. */
struct RadioConfig
{
  static constexpr uint8_t kVersion = 1;

  uint8_t version = kVersion;
  RadioMode mode = RadioMode::Off; ///< takes effect at the next boot

  // BLE (stage 17): OxiMate is always a peripheral (iPad, Mac); with `central` it also connects to
  // a BLE MIDI device it found in a scan (a controller, the Oxi One)
  bool central = false;
  char peerAddress[18] = ""; ///< remembered device, "aa:bb:cc:dd:ee:ff"; empty = none
  uint8_t peerAddressType = 0;
  char peerName[24] = "";

  // Wi-Fi (stage 18): OxiMate's own access point by default, or join a network
  bool joinNetwork = false;
  char ssid[33] = "";
  char password[65] = "";
};

namespace RadioSettings
{
RadioConfig load();
void save(const RadioConfig &config);
} // namespace RadioSettings

/*! A radio MIDI transport. begin() runs in setup() (before the MIDI task), poll() in the MIDI task
 *  (decode what arrived, hand complete messages to the handler, flush what was sent), the rest on
 *  the UI thread. */
class RadioTransport : public RadioPort
{
public:
  static constexpr int kStatusLines = 3;
  static constexpr int kStatusLength = 41;

  /*! A device found in a scan (BLE central mode) */
  struct Found
  {
    char name[24];
    char address[18];
    uint8_t addressType;
  };

  virtual bool begin(const RadioConfig &config, MidiHandler handler) = 0;
  virtual void poll() = 0;
  /*! Up to kStatusLines lines for the Radio page (UI thread) */
  virtual void status(char lines[kStatusLines][kStatusLength]) const = 0;

  // Optional, UI thread: scanning for devices to connect to (BLE central mode)
  virtual bool canScan() const { return false; }
  virtual void startScan() {}
  virtual bool scanning() const { return false; }
  virtual int scanResults(Found *found, int max) const { return 0; }
  /*! Connect to `device` now and remember it in `config` (saved by the caller) */
  virtual void connectTo(const Found &device, RadioConfig &config) {}
  virtual void forget(RadioConfig &config) {}
};
