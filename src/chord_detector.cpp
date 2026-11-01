#include "chord_detector.h"

#include <stdio.h>
#include <string.h>

using namespace MusicTheory;

static constexpr float kKeyDecay = 0.8f; // older chords count less for the key estimate

static bool sameChord(const ScoredChord &a, const ScoredChord &b)
{
  return a.root == b.root && a.type == b.type && a.bass == b.bass;
}

void ChordDetector::update(const NoteSet &set, uint32_t nowMillis)
{
  if (_frozen) return;
  if (set.version != _seenVersion) {
    _seenVersion = set.version;
    _changedAt = nowMillis;
    _settling = true;
    _historyPending = false;
    _resultVersion++; // shown as "settling"
  }
  if (_settling && nowMillis - _changedAt >= kSettleMillis) {
    _settling = false;
    detect(set);
    _stableSince = nowMillis;
    _historyPending = _result.kind == Kind::Chord;
    _resultVersion++;
  }
  if (_historyPending && nowMillis - _stableSince >= kHistoryMillis) {
    _historyPending = false;
    addToHistory();
    _resultVersion++;
  }
}

void ChordDetector::detect(const NoteSet &set)
{
  Result r = {};
  for (int note = 0; note < 128 && r.noteCount < kMaxNotes; note++) {
    if (set.notes[note >> 5] & (1u << (note & 31))) r.notes[r.noteCount++] = note;
  }
  for (int note = 0; note < 128; note++) {
    if (set.notes[note >> 5] & (1u << (note & 31))) r.pcs |= 1 << (note % 12);
  }
  int pitchClasses = countPitchClasses(r.pcs);
  r.bass = set.lowest == 0xFF ? 0 : set.lowest;
  uint8_t bassPc = r.bass % 12;

  if (pitchClasses == 0) {
    r.kind = Kind::Empty;
  } else if (pitchClasses == 1) {
    r.kind = Kind::Single;
  } else {
    ScoredChord candidates[8];
    int count = detectChords(r.pcs, bassPc, candidates, 8);
    // Two pitch classes are only a chord as a power chord; otherwise they're an interval
    bool power = count > 0 && kExtendedChords[candidates[0].type].count == 2;
    if (count == 0 || (pitchClasses == 2 && !power)) {
      r.kind = pitchClasses == 2 ? Kind::Interval : Kind::Unknown;
      int other = 0;
      for (int pc = 0; pc < 12; pc++) {
        if ((r.pcs & (1 << pc)) && pc != bassPc) other = pc;
      }
      r.interval = (other + 12 - bassPc) % 12;
    } else {
      r.kind = Kind::Chord;
      r.best = candidates[0];
      // Alternatives: different names within the window of the best score
      char bestName[16], name[16];
      leadSheetName(r.best, bestName, sizeof(bestName));
      for (int i = 1; i < count && r.alternativeCount < 2; i++) {
        if (candidates[i].score < r.best.score - kAlternativeWindow) break;
        leadSheetName(candidates[i], name, sizeof(name));
        if (strcmp(name, bestName) != 0) r.alternatives[r.alternativeCount++] = candidates[i];
      }
    }
  }
  _result = r;
}

void ChordDetector::addToHistory()
{
  const ScoredChord &chord = _result.best;
  if (_historyCount && sameChord(_history[_historyCount - 1], chord)) return;
  if (_historyCount == kHistory) {
    memmove(_history, _history + 1, sizeof(ScoredChord) * (kHistory - 1));
    _historyCount--;
  }
  _history[_historyCount++] = chord;

  // Key estimate: decaying pitch-class weights of the chords played
  for (float &w : _weights) w *= kKeyDecay;
  uint16_t tones = transpose(extendedMask(chord.type), chord.root);
  for (int pc = 0; pc < 12; pc++) {
    if (tones & (1 << pc)) _weights[pc] += 1.0f;
  }
  _weights[chord.root] += 0.5f; // the root says most about the key
}

void ChordDetector::clear()
{
  _result = {};
  _historyCount = 0;
  _settling = _historyPending = false;
  memset(_weights, 0, sizeof(_weights));
  _resultVersion++;
}

void ChordDetector::stableName(char *out, size_t size, bool flats) const
{
  if (_settling || _result.kind != Kind::Chord) {
    if (size) out[0] = '\0';
    return;
  }
  leadSheetName(_result.best, out, size, flats);
}
