#pragma once

#include "radio.h"
#include "router.h"
#include "ui.h"

/*! Radio page (stages 17, 18): radio mode (Off / BLE / Wi-Fi, applied by rebooting), the radio's
 *  status, its routes, and BLE central scanning. A = Reboot (apply the mode), B = Scan (BLE) or
 *  join/own network (Wi-Fi), C = back from the scan list / forget the remembered device. */
class RadioPage : public Page
{
public:
  RadioPage(RadioConfig &config, Router &router) : _config(config), _router(router) {}
  /*! At boot: the mode this boot runs with, and its transport (nullptr with the radio off) */
  void setRunning(RadioMode running, RadioTransport *transport)
  {
    _running = running;
    _transport = transport;
  }

  const char *title() const override { return "Radio"; }
  void buttonLabels(const char *labels[3]) const override;
  void onButton(int index) override;
  void onTouch(int x, int y) override;
  void tick() override;
  void draw() override;

private:
  MidiSource radioInput() const { return _running == RadioMode::Ble ? MidiSource::Ble : MidiSource::Net; }
  MidiOutput radioOutput() const { return _running == RadioMode::Ble ? MidiOutput::Ble : MidiOutput::Net; }
  /*! The chosen radio is in the other firmware (other app slot, see radio.h) */
  bool inOtherFirmware() const { return _config.mode != RadioMode::Off && _config.mode != kBuiltRadio; }
  void drawMain();
  void drawScan();

  RadioConfig &_config;
  Router &_router;
  RadioMode _running = RadioMode::Off; ///< the mode this boot started with
  RadioTransport *_transport = nullptr;

  bool _scanView = false;
  RadioTransport::Found _found[8];
  int _foundCount = 0;
  uint32_t _lastDraw = 0;
};
