#pragma once

#include "clock_engine.h"
#include "leds.h"
#include "tap_tempo.h"
#include "ui.h"

/*! Clock page (stage 8): tempo, source, transport, swing and per-output clock settings.
 *  Layout in docs/stages/stage-8.md "Clock page". */
class ClockPage : public Page
{
public:
  ClockPage(ClockEngine &clock, Leds &leds) : _clock(clock), _leds(leds) {}

  const char *title() const override { return "Clock"; }
  void buttonLabels(const char *labels[3]) const override;
  void onButton(int index) override;
  void onButtonHold(int index) override;
  void onTouch(int x, int y) override;
  void enter() override;
  void leave() override;
  void tick() override;
  void draw() override;

  /*! Feed the IMU; call every loop (taps only count when IMU tap is on) */
  void pollImu();

private:
  static constexpr uint8_t kLedOwner = 8;

  void drawMain();
  void drawDynamic();
  void drawOutputs();
  void changed();
  void tapNow(uint32_t micros);
  void renderLeds(const ClockEngine::Snapshot &snap);

  ClockEngine &_clock;
  Leds &_leds;
  TapTempo _tap;
  bool _outputsView = false;
  bool _visible = false;
  bool _partial = false; // next draw() only updates digits and position
  bool _labelRunning = false; // transport state shown on the A button

  // Last drawn values, to redraw the dynamic parts only when they change
  ClockEngine::Snapshot _shown = {};
  uint32_t _lastBarMillis = 0;
  uint16_t _lastBar = 0xFFFF;

  // IMU tap detection
  uint32_t _lastImuMicros = 0;
  uint32_t _lastTapMicros = 0;
  float _baseline = 1.0f;
};
