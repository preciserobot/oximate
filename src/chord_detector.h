#pragma once

#include <stddef.h>
#include <stdint.h>

#include "chord_input.h"
#include "music_theory.h"

/*! Stage 21: names what ChordInput reports (UI thread). Waits for a strummed chord to settle,
 *  then picks the best reading and up to 2 alternatives, keeps the last 4 distinct chords and a
 *  running key estimate. Pure C++ (no screen), so it is unit-tested on the host. */
class ChordDetector
{
public:
  static constexpr uint32_t kSettleMillis = 40;   ///< a rolled chord settles before it is named
  static constexpr uint32_t kHistoryMillis = 150; ///< a chord counts for the history once stable this long
  static constexpr int kHistory = 4;
  static constexpr int kMaxNotes = 16;            ///< notes listed on the page

  enum class Kind : uint8_t
  {
    Empty,    ///< nothing held
    Single,   ///< one pitch class
    Interval, ///< two pitch classes that are no power chord
    Chord,
    Unknown,  ///< 3+ pitch classes that match nothing well enough
  };

  struct Result
  {
    Kind kind;
    uint8_t notes[kMaxNotes]; ///< the held notes, lowest first
    uint8_t noteCount;
    uint16_t pcs;
    uint8_t bass;     ///< lowest note (MIDI note number)
    uint8_t interval; ///< Interval: semitones above the bass's pitch class
    MusicTheory::ScoredChord best;
    MusicTheory::ScoredChord alternatives[2];
    uint8_t alternativeCount;
  };

  /*! Feed the latest snapshot (every UI loop) */
  void update(const NoteSet &set, uint32_t nowMillis);
  void clear(); ///< forget the result, history and key

  void setFrozen(bool frozen) { _frozen = frozen; }
  bool frozen() const { return _frozen; }

  const Result &result() const { return _result; }
  bool settling() const { return _settling; }
  uint32_t version() const { return _resultVersion; } ///< increments whenever result, history or key change

  int historyCount() const { return _historyCount; }
  const MusicTheory::ScoredChord &history(int i) const { return _history[i]; } ///< 0 = oldest
  MusicTheory::KeyEstimate key() const { return MusicTheory::estimateKey(_weights); }

  /*! The current stable chord as text ("Cmaj7/E"), or "" when there is none. This is the hook
   *  for showing the chord elsewhere, e.g. on the Oxi One's screen once stage 3 runs. */
  void stableName(char *out, size_t size, bool flats = false) const;

private:
  void detect(const NoteSet &set);
  void addToHistory();

  Result _result = {};
  uint32_t _seenVersion = 0;
  uint32_t _changedAt = 0;
  bool _settling = false;
  bool _historyPending = false;
  uint32_t _stableSince = 0;
  bool _frozen = false;
  uint32_t _resultVersion = 0;

  MusicTheory::ScoredChord _history[kHistory];
  int _historyCount = 0;
  float _weights[12] = {};
};
