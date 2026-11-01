#include "ear_trainer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "music_theory.h"

using namespace MusicTheory;

// Answers offered per level (stage 7 plan, "Difficulty levels")
static const int8_t kIntervalLevels[4][12] = {
    {5, 7, 12, -1},
    {2, 3, 4, 5, 7, 12, -1},
    {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12},
    {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12},
};
static const int8_t kChordLevels[4][12] = {
    {Major, Minor, -1},
    {Major, Minor, Diminished, Augmented, -1},
    {Major, Minor, Diminished, Augmented, Sus2, Sus4, Major7, Dominant7, Minor7, -1},
    {Major, Minor, Diminished, Augmented, Sus2, Sus4, Major7, Dominant7, Minor7, HalfDiminished7, Diminished7, -1},
};
static const int8_t kDegreeLevels[4][12] = {
    {0, 4, 7, -1},
    {0, 2, 4, 5, 7, 9, 11, -1},
    {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11},
    {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11},
};

static constexpr uint8_t kLowestRoot = 48;  // C3
static constexpr uint8_t kHighestNote = 84; // C6
static constexpr uint8_t kMaxMistakes = 20;

void EarTrainer::begin(uint32_t seed) { _seed = seed ? seed : 1; }

uint32_t EarTrainer::random()
{
  // xorshift32: small and good enough for picking questions
  _seed ^= _seed << 13;
  _seed ^= _seed >> 17;
  _seed ^= _seed << 5;
  return _seed;
}

void EarTrainer::setExercise(Exercise exercise)
{
  _exercise = exercise;
  _result = Pending;
}

void EarTrainer::cycleLevel()
{
  uint8_t &level = _stats.level[_exercise];
  bool &autoLevel = _stats.autoLevel[_exercise];
  if (autoLevel) {
    autoLevel = false;
    level = 1;
  } else if (level >= 4) {
    autoLevel = true;
  } else {
    level++;
  }
  _stats.window[_exercise] = 0;
  _stats.windowCount[_exercise] = 0;
}

int EarTrainer::answers(int8_t *ids) const
{
  const int8_t(*table)[12] = _exercise == Intervals ? kIntervalLevels
                             : _exercise == Chords  ? kChordLevels
                             : _exercise == Degrees ? kDegreeLevels
                                                    : nullptr;
  if (!table) return 0;
  int count = 0;
  for (int i = 0; i < 12 && table[level() - 1][i] >= 0; i++) ids[count++] = table[level() - 1][i];
  return count;
}

int EarTrainer::answerCount() const
{
  int8_t ids[kMaxAnswers];
  return answers(ids);
}

int8_t EarTrainer::answerId(int index) const
{
  int8_t ids[kMaxAnswers];
  int count = answers(ids);
  return index >= 0 && index < count ? ids[index] : -1;
}

bool EarTrainer::answerAvailable(int8_t id) const
{
  int8_t ids[kMaxAnswers];
  int count = answers(ids);
  for (int i = 0; i < count; i++) {
    if (ids[i] == id) return true;
  }
  return false;
}

const char *EarTrainer::answerLabel(Exercise exercise, int8_t id)
{
  if (id < 0) return "";
  switch (exercise) {
    case Intervals: return id <= 12 ? kIntervalNames[id] : "";
    case Chords:    return id < kChordCount ? kChordQualities[id].name : "";
    case Degrees:   return id < 12 ? kDegreeNames[id] : "";
    default:        return "";
  }
}

/*! Weak spots come up more often: weight = 1 + recent mistakes on that answer */
int8_t EarTrainer::pickWeighted(const int8_t *ids, int count)
{
  uint32_t total = 0;
  for (int i = 0; i < count; i++) total += 1 + _stats.mistakes[_exercise][ids[i] % kMaxAnswers];
  uint32_t pick = random() % total;
  for (int i = 0; i < count; i++) {
    uint32_t weight = 1 + _stats.mistakes[_exercise][ids[i] % kMaxAnswers];
    if (pick < weight) return ids[i];
    pick -= weight;
  }
  return ids[count - 1];
}

void EarTrainer::addEvent(uint16_t at, uint16_t length, uint8_t note)
{
  if (_question.eventCount < kMaxEvents) _question.events[_question.eventCount++] = {at, length, note};
}

const EarTrainer::Question &EarTrainer::next()
{
  _question = {};
  _question.exercise = _exercise;
  _question.answer = -1;
  _result = Pending;
  _picked = -1;
  _firstNote = -1;
  _heldCount = 0;
  _melodyIndex = 0;

  int8_t ids[kMaxAnswers];
  int count = answers(ids);
  int lvl = level();
  // Levels 1-3 stay in C (C4 = 60); level 4 picks a random key per question
  uint8_t key = lvl >= 4 ? 54 + random() % 12 : 60;
  _question.key = key;

  switch (_exercise) {
    case Intervals: {
      int8_t semitones = pickWeighted(ids, count);
      uint8_t low = kLowestRoot + random() % (kHighestNote - 12 - kLowestRoot);
      uint8_t high = low + semitones;
      int direction = lvl >= 4 ? random() % 3 : 0; // rising, falling, together
      if (direction == 2) {
        addEvent(0, 1200, low);
        addEvent(0, 1200, high);
      } else {
        addEvent(0, 600, direction == 0 ? low : high);
        addEvent(700, 600, direction == 0 ? high : low);
      }
      _question.answer = semitones;
      _question.notes[0] = low;
      _question.notes[1] = high;
      _question.noteCount = 2;
      break;
    }
    case Chords: {
      int8_t quality = pickWeighted(ids, count);
      uint8_t root = kLowestRoot + random() % 18;
      int inversion = lvl >= 4 ? random() % kChordQualities[quality].count : 0;
      _question.noteCount = chordNotes(root, quality, inversion, _question.notes);
      for (int i = 0; i < _question.noteCount; i++) addEvent(0, 1400, _question.notes[i]);
      _question.answer = quality;
      break;
    }
    case Degrees: {
      int8_t degree = pickWeighted(ids, count);
      // Key context first: I - IV - V - I, then the note
      static const int8_t kCadence[4][3] = {{0, 4, 7}, {5, 9, 12}, {-5, -1, 2}, {0, 4, 7}};
      for (int chord = 0; chord < 4; chord++) {
        for (int8_t offset : kCadence[chord]) addEvent(chord * 500, 450, key + offset);
      }
      _question.notes[0] = key + degree;
      _question.noteCount = 1;
      addEvent(2300, 900, _question.notes[0]);
      _question.answer = degree;
      break;
    }
    default: { // Melody: 3-6 notes in the key, wider steps at higher levels
      int length = lvl + 2;
      int maxStep = lvl >= 4 ? 7 : lvl; // in scale steps
      int index = 2 + random() % 5;     // somewhere in the middle of a 10-step range
      for (int i = 0; i < length && i < kMaxNotes; i++) {
        if (i > 0) {
          int step = 1 + random() % maxStep;
          index += (random() & 1) ? step : -step;
          if (index < 0) index = -index;
          if (index > 9) index = 18 - index;
        }
        uint8_t note = key + (index / 7) * 12 + kMajorScale[index % 7];
        _question.notes[i] = note;
        addEvent(i * 500, 400, note);
      }
      _question.noteCount = length;
      break;
    }
  }
  return _question;
}

void EarTrainer::finish(bool correct)
{
  _result = correct ? Correct : Wrong;
  Exercise e = _exercise;
  _stats.total[e]++;
  if (correct) {
    _stats.correct[e]++;
    if (++_stats.streak > _stats.bestStreak) _stats.bestStreak = _stats.streak;
  } else {
    _stats.streak = 0;
  }
  if (_question.answer >= 0) {
    uint8_t &mistakes = _stats.mistakes[e][_question.answer % kMaxAnswers];
    if (!correct && mistakes < kMaxMistakes) mistakes++;
    if (correct && mistakes > 0) mistakes--;
  }

  // Auto level: decide after kWindow answers (>= 90% up, < 50% down)
  _stats.window[e] = (_stats.window[e] << 1) | (correct ? 1 : 0);
  if (++_stats.windowCount[e] >= kWindow) {
    int right = 0;
    for (int i = 0; i < kWindow; i++) right += (_stats.window[e] >> i) & 1;
    if (_stats.autoLevel[e]) {
      if (right >= 9 && _stats.level[e] < 4) _stats.level[e]++;
      if (right < 5 && _stats.level[e] > 1) _stats.level[e]--;
    }
    _stats.window[e] = 0;
    _stats.windowCount[e] = 0;
  }
}

EarTrainer::Result EarTrainer::answerTouch(int8_t id)
{
  if (_result != Pending || _exercise == Melody || _question.eventCount == 0) return _result;
  _picked = id;
  finish(id == _question.answer);
  return _result;
}

void EarTrainer::skip()
{
  if (_result == Pending && _question.eventCount) finish(false);
}

EarTrainer::Result EarTrainer::noteOn(uint8_t note)
{
  if (_result != Pending || _question.eventCount == 0) return _result;
  switch (_exercise) {
    case Intervals: {
      // The distance between two notes, in any octave, rising or falling
      if (_firstNote < 0 || note == _firstNote) {
        _firstNote = note;
        return Pending;
      }
      int distance = abs(note - _firstNote);
      while (distance > 12) distance -= 12;
      _picked = distance;
      finish(distance == _question.answer);
      break;
    }
    case Chords: {
      bool known = false;
      for (int i = 0; i < _heldCount; i++) known = known || _held[i] == note;
      if (!known && _heldCount < sizeof(_held)) _held[_heldCount++] = note;
      uint16_t wanted = pitchClassMask(_question.notes, _question.noteCount);
      uint16_t played = pitchClassMask(_held, _heldCount);
      if (countPitchClasses(played) < countPitchClasses(wanted)) return Pending;
      ChordMatch match = matchNotes(_held, _heldCount);
      _picked = match.found ? match.quality : -1;
      finish(played == wanted); // any inversion, any octave
      break;
    }
    case Degrees: {
      int degree = ((note - _question.key) % 12 + 12) % 12;
      _picked = degree;
      finish(degree == _question.answer);
      break;
    }
    default: // Melody: the pitch classes in order
      if (note % 12 != _question.notes[_melodyIndex] % 12) {
        finish(false);
      } else if (++_melodyIndex >= _question.noteCount) {
        finish(true);
      }
      break;
  }
  return _result;
}

void EarTrainer::noteOff(uint8_t note)
{
  for (int i = 0; i < _heldCount; i++) {
    if (_held[i] == note) {
      _held[i] = _held[--_heldCount];
      break;
    }
  }
}

void EarTrainer::describe(char *out, unsigned size) const
{
  char notes[48] = "";
  for (int i = 0; i < _question.noteCount; i++) {
    char name[6];
    noteName(_question.notes[i], name, sizeof(name));
    if (i) strncat(notes, " ", sizeof(notes) - strlen(notes) - 1);
    strncat(notes, name, sizeof(notes) - strlen(notes) - 1);
  }
  switch (_question.exercise) {
    case Intervals:
      snprintf(out, size, "%s: %s", kIntervalNames[_question.answer], notes);
      break;
    case Chords: {
      ChordMatch match = matchNotes(_question.notes, _question.noteCount);
      char name[32];
      chordName(match, name, sizeof(name));
      snprintf(out, size, "%s: %s", name, notes);
      break;
    }
    case Degrees:
      snprintf(out, size, "%s in %s: %s", kDegreeNames[_question.answer], pitchClassName(_question.key % 12), notes);
      break;
    default:
      snprintf(out, size, "%s", notes);
      break;
  }
}
