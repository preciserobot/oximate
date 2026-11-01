#pragma once

#include "diagnostics.h"
#include "leds.h"
#include "panic.h"
#include "ui.h"

class DinMidi;
class UsbMidi;
class MidiOutputs;

/*! Diagnostics page (stage 16): channel activity, message counters, system state.
 *  A = reset counters, B = next view, C = panic (click: level 2, hold: level 3). */
class DiagPage : public Page
{
public:
  DiagPage(Stats &stats, Panic &panic, MidiOutputs &outputs, DinMidi &din, UsbMidi &usb, Leds &leds)
      : _stats(stats), _panic(panic), _outputs(outputs), _din(din), _usb(usb), _leds(leds)
  {
  }

  const char *title() const override { return "Diag"; }
  void buttonLabels(const char *labels[3]) const override;
  void onButton(int index) override;
  void onButtonHold(int index) override;
  void enter() override;
  void leave() override;
  void tick() override;
  void draw() override;

  /*! UI loop duration, measured by the sketch */
  void loopTime(uint32_t micros);
  /*! Messages the UI couldn't take from the MIDI task in time */
  void eventsDropped(uint32_t count) { _eventsDropped = count; }

private:
  enum View
  {
    Channels,
    Counters,
    System,
    kViews,
  };
  static constexpr uint8_t kLedOwner = 16;
  static constexpr uint32_t kActiveMillis = 250;

  void line(int row, const char *text, uint16_t color = TFT_WHITE);
  void drawChannels();
  void drawCounters();
  void drawSystem();
  void renderLeds();

  Stats &_stats;
  Panic &_panic;
  MidiOutputs &_outputs;
  DinMidi &_din;
  UsbMidi &_usb;
  Leds &_leds;

  View _view = Channels;
  bool _clearAll = true;
  uint32_t _lastDraw = 0;

  // per-second rates
  uint32_t _rateAt = 0;
  uint32_t _previous[2][Stats::kTypes] = {};
  uint32_t _rate[Stats::kTypes] = {};

  uint32_t _loopSum = 0, _loopCount = 0, _loopMax = 0, _loopAvgShown = 0, _loopMaxShown = 0;
  uint32_t _eventsDropped = 0;
};
