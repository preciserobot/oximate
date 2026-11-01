#pragma once

#include "chord_detector.h"
#include "chord_input.h"
#include "clock_engine.h"
#include "leds.h"
#include "ui.h"

/*! Chord settings, persisted in NVS (key "chord"). Bump kVersion when the layout changes. */
struct ChordConfig
{
  static constexpr uint8_t kVersion = 1;

  uint8_t version = kVersion;
  uint8_t inputs = 3;      ///< bit 0 USB, bit 1 DIN
  uint8_t channel = 0;     ///< 0 = all, 1-16
  bool skipDrums = true;   ///< with channel 0: ignore channel 10
  uint8_t arp = 0;         ///< 0 off, 1 250 ms, 2 500 ms, 3 1000 ms, 4 one bar
  bool sustain = false;
  bool flats = false;      ///< sharps by default (decided 2026-11-01)
  bool simpleView = false; ///< only the current chord, large
};

/*! Chord page (stage 21): names the chord being played into OxiMate.
 *  A = clear (hold: freeze), B = full / simple view, C = sharps / flats; settings chips at the
 *  bottom (input, channel, arpeggio window, sustain). */
class ChordPage : public Page
{
public:
  static constexpr uint8_t kOwner = 21; ///< LED claim owner

  ChordPage(ChordInput &input, ChordDetector &detector, const ClockEngine &clock, Leds &leds)
      : _input(input), _detector(detector), _clock(clock), _leds(leds)
  {
  }

  void begin(); ///< load settings and hand them to ChordInput (call in setup())
  void poll();  ///< every loop, any page: delayed save, "1 bar" follows the tempo

  const char *title() const override { return "Chord"; }
  void buttonLabels(const char *labels[3]) const override;
  void onButton(int index) override;
  void onButtonHold(int index) override;
  void onTouch(int x, int y) override;
  void enter() override;
  void leave() override;
  void tick() override;
  void draw() override;

private:
  void applyFilter();
  void changed();
  uint32_t arpMillis() const;
  void drawFull();
  void drawSimple();
  void drawChordName(const char *name, int y, uint16_t color, bool large);
  void renderLeds();
  void load();
  void save();

  ChordInput &_input;
  ChordDetector &_detector;
  const ClockEngine &_clock;
  Leds &_leds;

  ChordConfig _config;
  bool _visible = false;
  uint32_t _shownVersion = 0;
  uint32_t _appliedArp = 0;
  uint32_t _changedMillis = 0;
  bool _dirty = false;
};
