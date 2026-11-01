#include "ear_page.h"

#include <Preferences.h>

#include "midi_task.h"
#include "music_theory.h"

using namespace Layout;
using namespace MusicTheory;

// Trainer geometry
static constexpr int kStatusY = 24, kPromptY = 42;
static constexpr int kGridY = 62, kGridH = 24, kGridW = 53;
static constexpr int kResultY = 118;
static constexpr int kExerciseY = 140, kExerciseH = 24;
static constexpr int kOptionsY = 170, kOptionsH = 22, kOptionsW = 106;
static constexpr int kInfoY = 200;

// Explorer geometry
static constexpr int kRootY = 24, kQualityY = 70, kCellH = 22, kCellW = 53;
static constexpr int kChordTextY = 118;

static constexpr uint32_t kFlashMillis = 400;
static constexpr uint32_t kSaveDelayMillis = 2000;
static constexpr uint32_t kQuestionDelayMillis = 150; // after B, before the question plays
static constexpr uint8_t kVelocity = 100;

static const char *const kExerciseNames[] = {"Interv", "Chords", "Degree", "Melody"};
static const char *const kExerciseLong[] = {"Intervals", "Chords", "Degrees", "Melody"};
static const char *const kPrompts[] = {"What interval did you hear?", "Which chord quality?",
                                       "Which scale degree?", "Play it back"};
static const char *const kNamespace = "oximate";
static const char *const kKey = "trainer";

void EarPage::begin()
{
  _trainer.begin(micros());
  Preferences prefs;
  if (prefs.begin(kNamespace, true)) {
    EarTrainer::Stats stored;
    if (prefs.getBytesLength(kKey) == sizeof(stored) && prefs.getBytes(kKey, &stored, sizeof(stored)) == sizeof(stored) &&
        stored.version == EarTrainer::Stats::kVersion) {
      _trainer.stats() = stored;
    }
    prefs.end();
  }
}

void EarPage::poll()
{
  if (_statsDirty && millis() - _statsChangedAt >= kSaveDelayMillis) {
    _statsDirty = false;
    Preferences prefs;
    if (prefs.begin(kNamespace, false)) {
      prefs.putBytes(kKey, &_trainer.stats(), sizeof(EarTrainer::Stats));
      prefs.end();
    }
  }
}

void EarPage::statsChanged()
{
  _statsDirty = true;
  _statsChangedAt = millis();
}

// ---------------------------------------------------------------------------- playback

void EarPage::sendNote(uint8_t note, uint8_t velocity)
{
  if (velocity) {
    _sounding[note >> 5] |= 1u << (note & 31);
  } else {
    _sounding[note >> 5] &= ~(1u << (note & 31));
  }
  if (speaker()) {
    if (velocity) {
      _speaker.noteOn(note, velocity, false);
    } else {
      _speaker.noteOff(note);
    }
    return;
  }
  // On DIN (and so the SAM2695 part claimed for channel 15), sent by the MIDI task
  uint8_t bytes[3] = {(uint8_t)((velocity ? 0x90 : 0x80) | (kChannel - 1)), note, velocity};
  MidiTask::send(MidiOutput::Din, bytes, 3, Origin::internal(kOwner));
}

void EarPage::schedule(uint32_t at, uint8_t note, uint8_t velocity)
{
  if (_scheduledCount < kMaxScheduled) _scheduled[_scheduledCount++] = {at, note, velocity};
}

void EarPage::play(const EarTrainer::PlayEvent *events, int count)
{
  stopAll();
  uint32_t start = millis() + kQuestionDelayMillis;
  for (int i = 0; i < count; i++) {
    schedule(start + events[i].atMillis, events[i].note, kVelocity);
    schedule(start + events[i].atMillis + events[i].lengthMillis, events[i].note, 0);
  }
}

/*! Cancel what's scheduled and end every note we started */
void EarPage::stopAll()
{
  _scheduledCount = 0;
  for (int note = 0; note < 128; note++) {
    if (_sounding[note >> 5] & (1u << (note & 31))) sendNote(note, 0);
  }
}

// ---------------------------------------------------------------------------- page

void EarPage::enter()
{
  Page::enter();
  _visible = true;
  _leds.claim(kOwner, 0, Leds::kCount);
  _part = _synth.claimPart(kChannel);
  if (_part >= 0) _synth.setPart(_part, kChannel, 0, 110); // acoustic grand piano
}

void EarPage::leave()
{
  _visible = false;
  stopAll();
  if (_part >= 0) _synth.releasePart(_part);
  _part = -1;
  _leds.release(kOwner);
  if (_vibrationOff) {
    M5.Power.setVibration(0);
    _vibrationOff = 0;
  }
}

void EarPage::buttonLabels(const char *labels[3]) const
{
  if (_explorer) {
    static char inversion[12];
    snprintf(inversion, sizeof(inversion), "Inv:%s", inversionName(_inversion));
    labels[0] = "Play";
    labels[1] = inversion;
    labels[2] = _arpeggio ? "Arpeggio" : "Block";
    return;
  }
  static char level[10];
  if (_trainer.autoLevel()) {
    snprintf(level, sizeof(level), "Auto %d", _trainer.level());
  } else {
    snprintf(level, sizeof(level), "Level %d", _trainer.level());
  }
  labels[0] = "Replay";
  labels[1] = _asked && _trainer.result() == EarTrainer::Pending ? "Skip" : "Next";
  labels[2] = level;
}

void EarPage::newQuestion()
{
  const EarTrainer::Question &q = _trainer.next();
  _asked = true;
  play(q.events, q.eventCount);
  markDirty();
}

void EarPage::answered()
{
  bool correct = _trainer.result() == EarTrainer::Correct;
  _flashCorrect = correct;
  _flashUntil = millis() + kFlashMillis;
  if (_trainer.stats().vibration) {
    M5.Power.setVibration(160);
    _vibrationOff = millis() + (correct ? 60 : 200);
  }
  statsChanged();
  markDirty();
  markFooterDirty();
}

void EarPage::onButton(int index)
{
  if (_explorer) {
    switch (index) {
      case 0: playChord(); break;
      case 1:
        _inversion = (_inversion + 1) % kChordQualities[_quality].count;
        playChord();
        break;
      case 2: _arpeggio = !_arpeggio; break;
    }
    markDirty();
    return;
  }
  switch (index) {
    case 0: // replay
      if (_asked) play(_trainer.question().events, _trainer.question().eventCount);
      break;
    case 1:
      if (_asked && _trainer.result() == EarTrainer::Pending) {
        _trainer.skip(); // shows the answer, counted as wrong
        answered();
      } else {
        newQuestion();
      }
      break;
    case 2:
      _trainer.cycleLevel();
      _asked = false;
      stopAll();
      statsChanged();
      break;
  }
  markDirty();
}

void EarPage::onTouch(int x, int y)
{
  if (_explorer) {
    if (y >= kRootY && y < kRootY + 2 * kCellH) {
      _root = ((y - kRootY) / kCellH) * 6 + (x / kCellW < 6 ? x / kCellW : 5);
      playChord();
    } else if (y >= kQualityY && y < kQualityY + 2 * kCellH) {
      int quality = ((y - kQualityY) / kCellH) * 6 + (x / kCellW < 6 ? x / kCellW : 5);
      if (quality < kChordCount) {
        _quality = quality;
        if (_inversion >= kChordQualities[_quality].count) _inversion = 0;
        playChord();
      }
    } else if (y >= kOptionsY + 26 && y < kOptionsY + 26 + kOptionsH && x < kOptionsW) {
      _explorer = false;
      stopAll();
      markFooterDirty();
    }
    markDirty();
    return;
  }

  if (y >= kGridY && y < kGridY + 2 * kGridH) {
    int index = ((y - kGridY) / kGridH) * 6 + (x / kGridW < 6 ? x / kGridW : 5);
    int8_t id = _trainer.answerId(index);
    if (id >= 0 && _asked && _trainer.result() == EarTrainer::Pending) {
      _trainer.answerTouch(id);
      answered();
    }
  } else if (y >= kExerciseY && y < kExerciseY + kExerciseH) {
    _trainer.setExercise((EarTrainer::Exercise)(x / 80 < 4 ? x / 80 : 3));
    _asked = false;
    stopAll();
    markFooterDirty();
  } else if (y >= kOptionsY && y < kOptionsY + kOptionsH) {
    if (x < kOptionsW) {
      _explorer = true;
      stopAll();
      markFooterDirty();
    } else if (x < 2 * kOptionsW) {
      _trainer.stats().vibration = !_trainer.stats().vibration;
      statsChanged();
    }
  }
  markDirty();
}

void EarPage::onMidi(const MidiMessage &msg, bool)
{
  if (!msg.isChannel()) return;
  uint8_t type = msg.status() & 0xF0;
  uint8_t channel = msg.channel();
  if (channel == 10 || channel == kChannel || (type != 0x90 && type != 0x80)) return;
  bool on = type == 0x90 && msg.bytes[2] > 0;
  uint8_t note = msg.bytes[1];

  if (_explorer) {
    if (on) {
      bool known = false;
      for (int i = 0; i < _heldCount; i++) known = known || _held[i] == note;
      if (!known && _heldCount < sizeof(_held)) _held[_heldCount++] = note;
    } else {
      for (int i = 0; i < _heldCount; i++) {
        if (_held[i] == note) {
          _held[i] = _held[--_heldCount];
          break;
        }
      }
    }
    recognise();
    return;
  }

  if (!_asked) return;
  if (!on) {
    _trainer.noteOff(note);
    return;
  }
  EarTrainer::Result before = _trainer.result();
  EarTrainer::Result after = _trainer.noteOn(note);
  if (before == EarTrainer::Pending && after != EarTrainer::Pending) {
    answered();
  } else if (_trainer.exercise() == EarTrainer::Melody) {
    markDirty(); // progress
  }
}

/*! Name the chord being held on a keyboard (3 or more notes) */
void EarPage::recognise()
{
  if (_heldCount < 3) return; // keep the last name until the next chord
  ChordMatch match = matchNotes(_held, _heldCount);
  char name[32];
  chordName(match, name, sizeof(name));
  snprintf(_recognised, sizeof(_recognised), "You played: %s", name);
  markDirty();
}

void EarPage::playChord()
{
  uint8_t notes[kMaxChordNotes];
  int count = chordNotes(48 + _root, _quality, _inversion, notes);
  EarTrainer::PlayEvent events[kMaxChordNotes];
  for (int i = 0; i < count; i++) {
    events[i] = {(uint16_t)(_arpeggio ? i * 250 : 0), (uint16_t)(_arpeggio ? 900 - i * 100 : 1200), notes[i]};
  }
  play(events, count);
}

void EarPage::tick()
{
  uint32_t now = millis();
  // Due notes, in time order of the table (offs and ons at the same time both go out)
  for (int i = 0; i < _scheduledCount;) {
    if ((int32_t)(now - _scheduled[i].at) >= 0) {
      sendNote(_scheduled[i].note, _scheduled[i].velocity);
      _scheduled[i] = _scheduled[--_scheduledCount];
    } else {
      i++;
    }
  }
  if (_vibrationOff && (int32_t)(now - _vibrationOff) >= 0) {
    M5.Power.setVibration(0);
    _vibrationOff = 0;
  }
  renderLeds();
}

void EarPage::renderLeds()
{
  if (!_visible) return;
  const RGBColor off(0, 0, 0);
  if (_explorer) {
    // The chord's tones in their pitch-class colours
    const ChordQuality &q = kChordQualities[_quality];
    for (int i = 0; i < Leds::kCount; i++) {
      _leds.setPixel(kOwner, i, i < q.count ? noteColor(_root + q.intervals[i]) : off);
    }
    return;
  }
  bool flashing = _flashUntil && (int32_t)(millis() - _flashUntil) < 0;
  int streak = _trainer.stats().streak;
  for (int i = 0; i < Leds::kCount; i++) {
    RGBColor color = off;
    if (flashing) {
      color = _flashCorrect ? RGBColor(0, 255, 0) : RGBColor(255, 0, 0);
    } else if (i < streak) {
      color = RGBColor(0, 96, 255); // streak meter
    }
    _leds.setPixel(kOwner, i, color);
  }
}

// ---------------------------------------------------------------------------- drawing

void EarPage::draw()
{
  M5Canvas &c = canvas();
  c.fillRect(0, kContentTop, c.width(), kContentHeight, TFT_BLACK);
  if (_explorer) {
    drawExplorer();
  } else {
    drawTrainer();
  }
}

void EarPage::drawTrainer()
{
  M5Canvas &c = canvas();
  const EarTrainer::Stats &s = _trainer.stats();
  EarTrainer::Exercise e = _trainer.exercise();
  char text[48];

  int percent = s.total[e] ? s.correct[e] * 100 / s.total[e] : 0;
  snprintf(text, sizeof(text), " %-9s %s%d  Streak %u  %d%%", kExerciseLong[e], _trainer.autoLevel() ? "Auto " : "Lvl ",
           _trainer.level(), s.streak, percent);
  c.setTextColor(TFT_WHITE, TFT_BLACK);
  c.setCursor(0, kStatusY);
  c.print(text);

  EarTrainer::Result result = _trainer.result();
  c.setCursor(0, kPromptY);
  if (!_asked) {
    c.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    c.print(" B: new question");
  } else if (result == EarTrainer::Pending) {
    c.setTextColor(TFT_WHITE, TFT_BLACK);
    if (e == EarTrainer::Melody) {
      snprintf(text, sizeof(text), " %s  %d/%d", kPrompts[e], _trainer.melodyProgress(), _trainer.question().noteCount);
      c.print(text);
    } else {
      c.print(" ");
      c.print(kPrompts[e]);
    }
  } else {
    c.setTextColor(result == EarTrainer::Correct ? TFT_GREEN : TFT_RED, TFT_BLACK);
    c.print(result == EarTrainer::Correct ? " Correct!" : " Not quite");
  }

  // Answer grid: green = the right answer, red = a wrong pick (after answering)
  int count = _trainer.answerCount();
  for (int i = 0; i < count; i++) {
    int8_t id = _trainer.answerId(i);
    bool done = _asked && result != EarTrainer::Pending;
    bool right = done && id == _trainer.question().answer;
    bool wrongPick = done && result == EarTrainer::Wrong && id == _trainer.picked();
    drawChip(c, (i % 6) * kGridW, kGridY + (i / 6) * kGridH, kGridW, kGridH, EarTrainer::answerLabel(e, id),
             right || wrongPick, wrongPick ? TFT_RED : kOnColor);
  }
  if (e == EarTrainer::Melody) {
    c.setTextColor(TFT_DARKGREY, TFT_BLACK);
    c.setCursor(0, kGridY + 16);
    c.print(" Play the melody back on a keyboard");
  }

  if (_asked && result != EarTrainer::Pending) {
    _trainer.describe(text, sizeof(text));
    c.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    c.setCursor(0, kResultY);
    c.print(" ");
    c.print(text);
  }

  for (int i = 0; i < EarTrainer::kExercises; i++) {
    drawChip(c, i * 80, kExerciseY, 80, kExerciseH, kExerciseNames[i], i == e);
  }
  drawChip(c, 0, kOptionsY, kOptionsW, kOptionsH, "Explorer>", true);
  drawChip(c, kOptionsW, kOptionsY, kOptionsW, kOptionsH, s.vibration ? "Vibrate on" : "Vibrate off", s.vibration);
  snprintf(text, sizeof(text), "Best %u", s.bestStreak);
  c.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  c.setCursor(2 * kOptionsW + 8, kOptionsY + 3);
  c.print(text);

  c.setFont(&fonts::Font0);
  c.setTextColor(TFT_DARKGREY, TFT_BLACK);
  c.setCursor(2, kInfoY + 6);
  c.print(speaker() ? "Plays on the built-in speaker" : "Plays on the synth, channel 15 (also reaches DIN OUT)");
  c.setFont(&fonts::AsciiFont8x16);
}

void EarPage::drawExplorer()
{
  M5Canvas &c = canvas();
  for (int i = 0; i < 12; i++) {
    drawChip(c, (i % 6) * kCellW, kRootY + (i / 6) * kCellH, kCellW, kCellH, pitchClassName(i), i == _root);
  }
  for (int q = 0; q < kChordCount; q++) {
    drawChip(c, (q % 6) * kCellW, kQualityY + (q / 6) * kCellH, kCellW, kCellH, kChordQualities[q].name, q == _quality);
  }

  uint8_t notes[kMaxChordNotes];
  int count = chordNotes(48 + _root, _quality, _inversion, notes);
  char text[48], name[16], note[6];
  chordName(_root, _quality, name, sizeof(name));
  snprintf(text, sizeof(text), " %s :", name);
  for (int i = 0; i < count; i++) {
    noteName(notes[i], note, sizeof(note));
    strncat(text, " ", sizeof(text) - strlen(text) - 1);
    strncat(text, note, sizeof(text) - strlen(text) - 1);
  }
  c.setTextColor(TFT_WHITE, TFT_BLACK);
  c.setCursor(0, kChordTextY);
  c.print(text);

  strcpy(text, " Intervals:");
  const ChordQuality &quality = kChordQualities[_quality];
  for (int i = 0; i < quality.count; i++) {
    strncat(text, " ", sizeof(text) - strlen(text) - 1);
    strncat(text, i == 0 ? "R" : kIntervalNames[quality.intervals[i] % 12], sizeof(text) - strlen(text) - 1);
  }
  c.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  c.setCursor(0, kChordTextY + 18);
  c.print(text);

  snprintf(text, sizeof(text), " Inversion: %s   %s", inversionName(_inversion), _arpeggio ? "arpeggio" : "block");
  c.setCursor(0, kChordTextY + 36);
  c.print(text);

  c.setTextColor(TFT_CYAN, TFT_BLACK);
  c.setCursor(0, kChordTextY + 54);
  c.print(_recognised[0] ? _recognised : " Play a chord on a keyboard to name it");

  drawChip(c, 0, kOptionsY + 26, kOptionsW, kOptionsH, "<Trainer", true);
}
