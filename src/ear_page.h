#pragma once

#include "ear_trainer.h"
#include "leds.h"
#include "sam_synth.h"
#include "speaker_synth.h"
#include "ui.h"

/*! Ear page (stage 7): the ear trainer and the chord explorer, as two modes of one page
 *  (switched with a chip) so there is one page less to swipe through.
 *
 *  Questions and chords play on a SAM2695 part claimed on channel 15 (the config channel 16
 *  is SamSynth's), or on the built-in speaker when the Synth page's output is "Speaker".
 *  Keyboard answers come from any input on any channel except 10 (drums) and 15 (our own
 *  notes, which a device with MIDI thru could echo back). */
class EarPage : public Page
{
public:
  static constexpr uint8_t kChannel = 15;
  static constexpr uint8_t kOwner = 7; ///< Origin::internal and LED claim owner

  EarPage(SamSynth &synth, SpeakerSynth &speaker, Leds &leds) : _synth(synth), _speaker(speaker), _leds(leds) {}

  void begin(); ///< load statistics (call in setup())
  void poll();  ///< every loop, any page: delayed statistics save

  const char *title() const override { return "Ear"; }
  void buttonLabels(const char *labels[3]) const override;
  void onButton(int index) override;
  void onTouch(int x, int y) override;
  void onMidi(const MidiMessage &msg, bool forwarded) override;
  void enter() override;
  void leave() override;
  void tick() override;
  void draw() override;

private:
  struct Scheduled
  {
    uint32_t at;
    uint8_t note;
    uint8_t velocity; // 0 = note-off
  };
  static constexpr int kMaxScheduled = 64;

  // Playback
  void play(const EarTrainer::PlayEvent *events, int count);
  void schedule(uint32_t at, uint8_t note, uint8_t velocity);
  void sendNote(uint8_t note, uint8_t velocity);
  void stopAll();
  bool speaker() { return _synth.config().speaker; }

  // Trainer
  void newQuestion();
  void answered();
  void drawTrainer();

  // Explorer
  void playChord();
  void drawExplorer();
  void recognise();

  void renderLeds();
  void statsChanged();

  SamSynth &_synth;
  SpeakerSynth &_speaker;
  Leds &_leds;
  EarTrainer _trainer;
  bool _explorer = false;
  bool _visible = false;
  int8_t _part = -1;

  Scheduled _scheduled[kMaxScheduled];
  int _scheduledCount = 0;
  uint32_t _sounding[4] = {}; // notes we have switched on, to release on leave

  // Trainer feedback
  bool _asked = false;
  uint32_t _flashUntil = 0;
  bool _flashCorrect = false;
  uint32_t _vibrationOff = 0;
  bool _statsDirty = false;
  uint32_t _statsChangedAt = 0;

  // Explorer
  uint8_t _root = 0;    // pitch class
  uint8_t _quality = 0; // MusicTheory::ChordId
  uint8_t _inversion = 0;
  bool _arpeggio = false;
  uint8_t _held[16];
  uint8_t _heldCount = 0;
  char _recognised[40] = "";
};
