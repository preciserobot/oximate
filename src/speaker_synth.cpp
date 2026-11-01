#include "speaker_synth.h"

#include <M5Unified.h>
#include <math.h>

// One cycle of a soft, rounded wave (unsigned 8-bit): gentler than M5Unified's default square
static const uint8_t kSoftWave[16] = {128, 176, 218, 245, 255, 245, 218, 176, 128, 80, 38, 11, 1, 11, 38, 80};
// Short noise-ish blip for hi-hats and snares
static const uint8_t kNoiseWave[16] = {128, 250, 10, 200, 60, 230, 30, 170, 90, 255, 0, 140, 210, 20, 240, 70};

static constexpr uint32_t kDrumMillis = 70;

static float noteFrequency(uint8_t note) { return 440.0f * powf(2.0f, (note - 69) / 12.0f); }

void SpeakerSynth::begin()
{
  M5.Speaker.begin();
  setVolume(100);
}

void SpeakerSynth::setVolume(uint8_t level) { M5.Speaker.setVolume(level * 2); }

int SpeakerSynth::allocate()
{
  int oldest = 0;
  for (int i = 0; i < kVoices; i++) {
    if (_voices[i].note < 0) return i;
    if (_voices[i].started < _voices[oldest].started) oldest = i;
  }
  M5.Speaker.stop(oldest); // all busy: steal the oldest note
  return oldest;
}

void SpeakerSynth::noteOn(uint8_t note, uint8_t velocity, bool drums)
{
  int v = allocate();
  uint32_t now = millis();
  _voices[v] = {(int16_t)note, now, drums ? now + kDrumMillis : 0};
  M5.Speaker.setChannelVolume(v, 64 + velocity);
  if (drums) {
    // GM drum map, roughly: kicks and toms low, snares mid noise, cymbals and hats high noise
    bool low = note <= 36 || (note >= 41 && note <= 50 && note % 2 == 1);
    bool noise = note == 38 || note == 40 || note >= 42;
    float frequency = low ? 55.0f + (note - 35) * 8.0f : (noise ? 2000.0f + (note - 38) * 60.0f : 180.0f);
    M5.Speaker.tone(frequency, kDrumMillis, v, true, noise ? kNoiseWave : kSoftWave, 16);
  } else {
    M5.Speaker.tone(noteFrequency(note), UINT32_MAX, v, true, kSoftWave, 16);
  }
}

void SpeakerSynth::noteOff(uint8_t note)
{
  for (int i = 0; i < kVoices; i++) {
    if (_voices[i].note == note && _voices[i].until == 0) {
      M5.Speaker.stop(i);
      _voices[i].note = -1;
    }
  }
}

void SpeakerSynth::allOff()
{
  M5.Speaker.stop();
  for (Voice &voice : _voices) voice.note = -1;
}

void SpeakerSynth::test(bool drums, uint32_t millisToPlay)
{
  allOff();
  static const uint8_t kChord[3] = {60, 64, 67};
  static const uint8_t kDrums[3] = {36, 38, 42};
  uint32_t until = millis() + millisToPlay;
  for (uint8_t note : drums ? kDrums : kChord) {
    noteOn(note, 100, drums);
  }
  for (Voice &voice : _voices) { // the chord ends on its own (drum blips already do)
    if (voice.note >= 0 && !drums) voice.until = until;
  }
}

void SpeakerSynth::update()
{
  uint32_t now = millis();
  for (int i = 0; i < kVoices; i++) {
    Voice &voice = _voices[i];
    if (voice.note >= 0 && voice.until && (int32_t)(now - voice.until) >= 0) {
      M5.Speaker.stop(i);
      voice.note = -1;
    }
  }
}
