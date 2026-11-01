#pragma once

#include <stdint.h>

/*! Ear-training logic (stage 7): question generation, answer checking, levels and statistics.
 *  Pure C++ (no Arduino), so it is unit-tested on the host; the page plays the questions. */
class EarTrainer
{
public:
  enum Exercise : uint8_t
  {
    Intervals,
    Chords,
    Degrees,
    Melody,
    kExercises,
  };

  enum Result : uint8_t
  {
    Pending,
    Correct,
    Wrong,
  };

  static constexpr int kMaxAnswers = 12;
  static constexpr int kMaxEvents = 24;
  static constexpr int kMaxNotes = 8;
  static constexpr int kWindow = 10; ///< answers per auto-level decision

  /*! One note of a question's playback, relative to its start */
  struct PlayEvent
  {
    uint16_t atMillis;
    uint16_t lengthMillis;
    uint8_t note;
  };

  struct Question
  {
    Exercise exercise;
    int8_t answer;          ///< interval semitones, chord quality, degree semitones; -1 for melody
    uint8_t key;            ///< tonic note (degrees, melody)
    uint8_t notes[kMaxNotes]; ///< the notes to be recognised, lowest/first first
    uint8_t noteCount;
    PlayEvent events[kMaxEvents];
    uint8_t eventCount;
  };

  /*! Persisted in NVS (key "trainer"); bump kVersion when the layout changes */
  struct Stats
  {
    static constexpr uint8_t kVersion = 1;

    uint8_t version = kVersion;
    uint8_t level[kExercises] = {1, 1, 1, 1};           ///< 1-4
    bool autoLevel[kExercises] = {false, false, false, false};
    uint16_t correct[kExercises] = {};
    uint16_t total[kExercises] = {};
    uint16_t streak = 0, bestStreak = 0;
    uint8_t mistakes[kExercises][kMaxAnswers] = {};    ///< weights weak spots in question choice
    uint16_t window[kExercises] = {};                   ///< last answers, bit set = correct
    uint8_t windowCount[kExercises] = {};
    bool vibration = true;
  };

  void begin(uint32_t seed);
  Stats &stats() { return _stats; }

  void setExercise(Exercise exercise);
  Exercise exercise() const { return _exercise; }
  int level() const { return _stats.level[_exercise]; }
  bool autoLevel() const { return _stats.autoLevel[_exercise]; }
  void cycleLevel(); ///< 1 -> 2 -> 3 -> 4 -> Auto -> 1

  const Question &next();
  const Question &question() const { return _question; }
  Result result() const { return _result; }
  int8_t picked() const { return _picked; } ///< the touched answer, -1 if none
  int melodyProgress() const { return _melodyIndex; }

  /*! Answers offered on the touch grid for the current exercise and level (none for melody) */
  int answerCount() const;
  int8_t answerId(int index) const;
  static const char *answerLabel(Exercise exercise, int8_t id);
  bool answerAvailable(int8_t id) const;

  Result answerTouch(int8_t id);
  void skip(); ///< moving on without an answer counts as wrong
  Result noteOn(uint8_t note); ///< keyboard answers: any octave
  void noteOff(uint8_t note);

  /*! Describes the answer, e.g. "M3 (C4 E4)" */
  void describe(char *out, unsigned size) const;

private:
  uint32_t random();
  int8_t pickWeighted(const int8_t *ids, int count);
  int answers(int8_t *ids) const;
  void addEvent(uint16_t at, uint16_t length, uint8_t note);
  void finish(bool correct);

  Stats _stats;
  Exercise _exercise = Intervals;
  Question _question = {};
  Result _result = Pending;
  int8_t _picked = -1;
  uint32_t _seed = 1;

  // Keyboard answer state
  int16_t _firstNote = -1;
  uint8_t _held[16];
  uint8_t _heldCount = 0;
  uint8_t _melodyIndex = 0;
};
