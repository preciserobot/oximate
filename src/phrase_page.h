#pragma once

#include "leds.h"
#include "phrase_tracker.h"
#include "ui.h"

/*! Phrase page (stage 22): where you are in the phrase.
 *  Big counter, phrase progress bar, bars left and the section level; settings chips.
 *  A = Reset (the current bar becomes bar 1), B = Bar 1 (tap on a downbeat; hold = restart the
 *  phrase but keep the section count), C = normal / big view. */
class PhrasePage : public Page
{
public:
  PhrasePage(PhraseTracker &tracker, Leds &leds) : _tracker(tracker), _leds(leds) {}

  const char *title() const override { return "Phrase"; }
  void buttonLabels(const char *labels[3]) const override;
  void onButton(int index) override;
  void onButtonHold(int index) override;
  void onTouch(int x, int y) override;
  void enter() override;
  void leave() override;
  void tick() override;
  void draw() override;

private:
  static constexpr uint8_t kLedOwner = 22;

  void drawNormal(const PhraseSnapshot &s);
  void drawBig(const PhraseSnapshot &s);
  void drawProgress(const PhraseSnapshot &s, int y, int height);
  void renderLeds(const PhraseSnapshot &s);
  void changed();

  PhraseTracker &_tracker;
  Leds &_leds;
  bool _big = false;
  bool _visible = false;
  bool _claimed = false;

  PhraseSnapshot _shown = {};
  uint32_t _flashUntil = 0; ///< LEDs flash white on bar 1
  uint32_t _lastPhraseStarts = 0;
};
