#pragma once

#include <M5Unified.h>
#include "midi_message.h"

/*! The 10 RGB LEDs (SK6812, GPIO25) of the M5GO Bottom2, as two strips of 5.
 *  Unclaimed LEDs show input activity: LEDs 0-4 (right side of the base) flash for USB,
 *  5-9 (left side) for DIN.
 *  A mode claims a range of LEDs and sets them with setPixel() until it releases them
 *  (README "Shared infrastructure": LED claims). UI thread only. */
class Leds
{
public:
  static constexpr int kCount = 10;
  static constexpr int kPerSide = 5;

  void begin();
  void setLevel(uint8_t level); ///< 0 off, 1 low, 2 medium, 3 high
  void activity(const MidiMessage &msg);
  void update(); ///< call every loop; renders at most ~60 fps

  void claim(uint8_t owner, int first, int count); ///< owner: stage number (non-zero)
  void release(uint8_t owner);
  void setPixel(uint8_t owner, int index, RGBColor color); ///< ignored unless owner holds the LED

  /*! Warnings and progress above everything else (panic); nullptr removes the overlay */
  void setOverlay(const RGBColor *colors);

private:
  struct Flash
  {
    float intensity = 0;
    RGBColor color;
  };

  Flash _flash[2]; // indexed by MidiSource
  uint8_t _owner[kCount] = {0};
  RGBColor _pixel[kCount];
  RGBColor _overlay[kCount];
  bool _overlayOn = false;
  uint32_t _lastFrame = 0;
  bool _lit = false;
};

/*! Colour for a pitch class (C = red, around the colour wheel to B) */
RGBColor noteColor(uint8_t note);
