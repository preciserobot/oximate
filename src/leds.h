#pragma once

#include <M5Unified.h>
#include "midi_message.h"

/*! The 10 RGB LEDs (SK6812, GPIO25) of the M5GO Bottom2, as two strips of 5.
 *  Default use is input activity: LEDs 0-4 flash for USB, 5-9 for DIN.
 *  Later modes (sequencers, clock) take the LEDs over via setPixel(). */
class Leds
{
public:
  static constexpr int kCount = 10;
  static constexpr int kPerSide = 5;

  void begin();
  void setLevel(uint8_t level); ///< 0 off, 1 low, 2 medium, 3 high
  void activity(const MidiMessage &msg);
  void update(); ///< call every loop; renders at most ~60 fps

private:
  struct Flash
  {
    float intensity = 0;
    RGBColor color;
  };

  Flash _flash[2]; // indexed by MidiSource
  uint32_t _lastFrame = 0;
  bool _lit = false;
};

/*! Colour for a pitch class (C = red, around the colour wheel to B) */
RGBColor noteColor(uint8_t note);
