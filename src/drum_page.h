#pragma once

#include "clock_engine.h"
#include "drum_machine.h"
#include "leds.h"
#include "ui.h"

/*! Drums page (stage 5). Three views, C cycles them:
 *  Grid: one lane at a time as 16 pads (page 1/2 of 32 steps) plus an overview of all lanes;
 *  Sound: the selected lane's note and velocities, pad channel, USB output;
 *  Pattern: 16 pattern slots (tap = queue), length, chain, clear.
 *  A = Play/Stop (Arm/Disarm when following an external clock), B = mute lane (hold: solo;
 *  in the Sound view: audition). */
class DrumPage : public Page
{
public:
  DrumPage(DrumMachine &drums, ClockEngine &clock, Leds &leds) : _drums(drums), _clock(clock), _leds(leds) {}

  const char *title() const override { return "Drums"; }
  void buttonLabels(const char *labels[3]) const override;
  void onButton(int index) override;
  void onButtonHold(int index) override;
  void onTouch(int x, int y) override;
  void enter() override;
  void leave() override;
  void tick() override;
  void draw() override;

private:
  enum View
  {
    Grid,
    Sound,
    Pattern,
    kViews,
  };
  static constexpr uint8_t kLedOwner = 5;

  void drawLanes();
  void drawGrid();
  void drawSound();
  void drawPattern();
  void touchGrid(int x, int y);
  void touchSound(int x, int y);
  void touchPattern(int x, int y);
  void renderLeds();
  bool following() const;
  int editPattern() const { return _drums.playingPattern(); }

  DrumMachine &_drums;
  ClockEngine &_clock;
  Leds &_leds;

  View _view = Grid;
  int _lane = 0;
  int _padPage = 0;          ///< 0 = steps 1-16, 1 = steps 17-32
  bool _chainEdit = false;   ///< tapping pattern slots appends them to the chain
  uint8_t _chain[DrumMachine::kPatterns];
  uint8_t _chainLength = 0;

  int _shownPosition = -2;
  uint32_t _shownHits = 0;
  uint32_t _hitFlashMillis = 0;
  bool _labelRunning = false;
};
