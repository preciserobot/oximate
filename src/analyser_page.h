#pragma once

#include "clock_analyser.h"
#include "leds.h"
#include "ui.h"

/*! Analyser page (stage 9): quality of the clock arriving on USB and DIN.
 *  A = reset, B = next view (Stats, Histogram, Drift, Phase), C = input shown in the graphs,
 *  hold C = CSV streaming on the serial port. */
class AnalyserPage : public Page
{
public:
  AnalyserPage(ClockAnalyser &analyser, Leds &leds) : _analyser(analyser), _leds(leds) {}

  const char *title() const override { return "Analyse"; }
  void buttonLabels(const char *labels[3]) const override;
  void onButton(int index) override;
  void onButtonHold(int index) override;
  void enter() override;
  void leave() override;
  void tick() override;
  void draw() override;

  /*! Print queued CSV lines without ever waiting for the serial port; call every loop */
  void pollCsv();

private:
  enum View : uint8_t
  {
    Stats,
    Histogram,
    Drift,
    Phase,
    kViews,
  };
  static constexpr uint8_t kLedOwner = 9;

  void drawStats();
  void drawHistogram();
  void drawDrift();
  void drawPhase();
  void plotArea(const char *title);
  void renderLeds();

  ClockAnalyser &_analyser;
  Leds &_leds;
  View _view = Stats;
  MidiSource _graphInput = MidiSource::Din;
  bool _visible = false;
  uint32_t _lastDraw = 0;
  uint32_t _lastLeds = 0;
};
