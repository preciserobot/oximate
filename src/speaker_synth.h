#pragma once

#include <stdint.h>

/*! A small synth on the Core2's built-in speaker (stage 6 "built-in speaker" output).
 *  The SAM2695's audio only reaches the Unit MIDI's headphone jack, so this plays notes with
 *  M5Unified's Speaker: 8 voices (its 8 tone channels), a soft wave for melodic notes and short
 *  blips for drums. UI thread only (it is fed from the event ring). */
class SpeakerSynth
{
public:
  static constexpr int kVoices = 8;

  void begin();
  void setVolume(uint8_t level); ///< 0-127
  void noteOn(uint8_t note, uint8_t velocity, bool drums);
  void noteOff(uint8_t note);
  void allOff();
  /*! Test chord (C major) or drum pattern (kick, snare, hat) for `millis` */
  void test(bool drums, uint32_t millis);
  void update(); ///< call every loop: ends timed notes

private:
  struct Voice
  {
    int16_t note = -1; // -1 = free
    uint32_t started = 0;
    uint32_t until = 0; // 0 = until note-off
  };

  int allocate();

  Voice _voices[kVoices];
};
