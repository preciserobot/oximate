#include "music_theory.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

namespace MusicTheory
{
const ChordQuality kChordQualities[kChordCount] = {
    {"maj", {0, 4, 7}, 3},       {"min", {0, 3, 7}, 3},      {"dim", {0, 3, 6}, 3},      {"aug", {0, 4, 8}, 3},
    {"sus2", {0, 2, 7}, 3},      {"sus4", {0, 5, 7}, 3},     {"maj7", {0, 4, 7, 11}, 4}, {"7", {0, 4, 7, 10}, 4},
    {"min7", {0, 3, 7, 10}, 4},  {"m7b5", {0, 3, 6, 10}, 4}, {"dim7", {0, 3, 6, 9}, 4},  {"add9", {0, 2, 4, 7}, 4},
};

const char *const kIntervalNames[13] = {"P1", "m2", "M2", "m3", "M3", "P4", "TT",
                                        "P5", "m6", "M6", "m7", "M7", "P8"};

const char *const kDegreeNames[12] = {"1", "b2", "2", "b3", "3", "4", "#4", "5", "b6", "6", "b7", "7"};

const uint8_t kMajorScale[7] = {0, 2, 4, 5, 7, 9, 11};

static const char *const kSharpNames[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
static const char *const kFlatNames[12] = {"C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B"};
static const char *const kInversionNames[kMaxChordNotes] = {"root", "1st", "2nd", "3rd", "4th", "5th"};

const char *pitchClassName(uint8_t pitchClass, bool flats) { return (flats ? kFlatNames : kSharpNames)[pitchClass % 12]; }

void noteName(uint8_t note, char *out, size_t size, bool flats)
{
  snprintf(out, size, "%s%d", pitchClassName(note % 12, flats), note / 12 - 1);
}

uint16_t chordMask(int quality)
{
  if (quality < 0 || quality >= kChordCount) return 0;
  uint16_t mask = 0;
  const ChordQuality &q = kChordQualities[quality];
  for (int i = 0; i < q.count; i++) mask |= 1 << (q.intervals[i] % 12);
  return mask;
}

uint16_t transpose(uint16_t mask, int semitones)
{
  semitones = ((semitones % 12) + 12) % 12;
  return ((mask << semitones) | (mask >> (12 - semitones))) & 0xFFF;
}

uint16_t pitchClassMask(const uint8_t *notes, int count)
{
  uint16_t mask = 0;
  for (int i = 0; i < count; i++) mask |= 1 << (notes[i] % 12);
  return mask;
}

int countPitchClasses(uint16_t mask)
{
  int count = 0;
  for (int i = 0; i < 12; i++) count += (mask >> i) & 1;
  return count;
}

int chordNotes(uint8_t rootNote, int quality, int inversion, uint8_t *out)
{
  if (quality < 0 || quality >= kChordCount) return 0;
  const ChordQuality &q = kChordQualities[quality];
  if (inversion < 0 || inversion >= q.count) return 0;
  for (int i = 0; i < q.count; i++) {
    int tone = (i + inversion) % q.count; // the inverted tones follow, an octave up
    out[i] = rootNote + q.intervals[tone] + (i + inversion >= q.count ? 12 : 0);
  }
  return q.count;
}

/*! Which chord tone (0 = root) the pitch class is, or -1 */
static int toneIndex(uint8_t root, int quality, uint8_t pitchClass)
{
  const ChordQuality &q = kChordQualities[quality];
  for (int i = 0; i < q.count; i++) {
    if ((root + q.intervals[i]) % 12 == pitchClass % 12) return i;
  }
  return -1;
}

ChordMatch matchChord(uint16_t mask, uint8_t bassPitchClass)
{
  ChordMatch best = {false, 0, 0, 0, (uint8_t)(bassPitchClass % 12)};
  for (int quality = 0; quality < kChordCount; quality++) {
    uint16_t shape = chordMask(quality);
    for (int root = 0; root < 12; root++) {
      if (transpose(shape, root) != mask) continue;
      bool bassIsRoot = root == bassPitchClass % 12;
      if (!best.found || (bassIsRoot && best.root != bassPitchClass % 12)) {
        int index = toneIndex(root, quality, bassPitchClass);
        best = {true, (uint8_t)root, (uint8_t)quality, (uint8_t)(index < 0 ? 0 : index), (uint8_t)(bassPitchClass % 12)};
      }
    }
  }
  return best;
}

ChordMatch matchNotes(const uint8_t *notes, int count)
{
  if (count <= 0) return {false, 0, 0, 0, 0};
  uint8_t lowest = notes[0];
  for (int i = 1; i < count; i++) {
    if (notes[i] < lowest) lowest = notes[i];
  }
  return matchChord(pitchClassMask(notes, count), lowest % 12);
}

void chordName(uint8_t root, int quality, char *out, size_t size, bool flats)
{
  snprintf(out, size, "%s %s", pitchClassName(root, flats), kChordQualities[quality].name);
}

void chordName(const ChordMatch &match, char *out, size_t size, bool flats)
{
  if (!match.found) {
    snprintf(out, size, "?");
    return;
  }
  char name[16];
  chordName(match.root, match.quality, name, sizeof(name), flats);
  if (match.inversion == 0) {
    snprintf(out, size, "%s", name);
  } else {
    snprintf(out, size, "%s (%s inv)", name, inversionName(match.inversion));
  }
}

const char *inversionName(int inversion)
{
  return inversion >= 0 && inversion < kMaxChordNotes ? kInversionNames[inversion] : "?";
}

// ---------------------------------------------------------------------------------------------
// Stage 21: extended chords and scoring

static constexpr uint16_t bit(int interval) { return 1 << interval; }
static constexpr uint16_t kFifth = bit(7);

const ExtendedChord kExtendedChords[] = {
    {"5", {0, 7}, 2, 0},
    {"", {0, 4, 7}, 3, kFifth},
    {"m", {0, 3, 7}, 3, kFifth},
    {"dim", {0, 3, 6}, 3, 0},
    {"aug", {0, 4, 8}, 3, 0},
    {"sus2", {0, 2, 7}, 3, kFifth},
    {"sus4", {0, 5, 7}, 3, kFifth},
    {"7sus4", {0, 5, 7, 10}, 4, kFifth},
    {"6", {0, 4, 7, 9}, 4, 0},  // the 5th is required: without it C6 is just Am/C
    {"m6", {0, 3, 7, 9}, 4, 0},
    {"6/9", {0, 4, 7, 9, 2}, 5, kFifth},
    {"maj7", {0, 4, 7, 11}, 4, kFifth},
    {"7", {0, 4, 7, 10}, 4, kFifth},
    {"m7", {0, 3, 7, 10}, 4, kFifth},
    {"m(maj7)", {0, 3, 7, 11}, 4, kFifth},
    {"m7b5", {0, 3, 6, 10}, 4, 0},
    {"dim7", {0, 3, 6, 9}, 4, 0},
    {"aug7", {0, 4, 8, 10}, 4, 0},
    {"maj7#5", {0, 4, 8, 11}, 4, 0},
    {"7b5", {0, 4, 6, 10}, 4, 0},
    {"add9", {0, 4, 7, 2}, 4, kFifth},
    {"madd9", {0, 3, 7, 2}, 4, kFifth},
    {"add11", {0, 4, 7, 5}, 4, kFifth},
    {"maj9", {0, 4, 7, 11, 2}, 5, kFifth},
    {"9", {0, 4, 7, 10, 2}, 5, kFifth},
    {"m9", {0, 3, 7, 10, 2}, 5, kFifth},
    {"7b9", {0, 4, 7, 10, 1}, 5, kFifth},
    {"7#9", {0, 4, 7, 10, 3}, 5, kFifth},
    {"11", {0, 4, 7, 10, 2, 5}, 6, kFifth | bit(2) | bit(4)}, // the 3rd is often left out under the 11th
    {"m11", {0, 3, 7, 10, 2, 5}, 6, kFifth | bit(2)},
    {"7#11", {0, 4, 7, 10, 6}, 5, kFifth},
    {"maj7#11", {0, 4, 7, 11, 6}, 5, kFifth},
    {"maj13", {0, 4, 7, 11, 2, 9}, 6, kFifth | bit(2)},
    {"13", {0, 4, 7, 10, 2, 9}, 6, kFifth | bit(2)},
    {"m13", {0, 3, 7, 10, 2, 9}, 6, kFifth | bit(2)},
    {"7b13", {0, 4, 7, 10, 8}, 5, kFifth},
    {"13b9", {0, 4, 7, 10, 1, 9}, 6, kFifth},
};
const int kExtendedChordCount = sizeof(kExtendedChords) / sizeof(kExtendedChords[0]);

uint16_t extendedMask(int type)
{
  uint16_t mask = 0;
  const ExtendedChord &chord = kExtendedChords[type];
  for (int i = 0; i < chord.count; i++) mask |= bit(chord.tones[i] % 12);
  return mask;
}

int scoreChord(uint16_t pcs, uint8_t bass, uint8_t root, int type)
{
  const ExtendedChord &chord = kExtendedChords[type];
  uint16_t shape = extendedMask(type);
  uint16_t played = transpose(pcs, 12 - root); // the set relative to this root
  int score = 0;
  for (int i = 0; i < chord.count; i++) {
    int interval = chord.tones[i] % 12;
    if (played & bit(interval)) {
      score += 10;
      if (interval == 0) score += 4;                                   // root
      if (interval == 3 || interval == 4 || (i == 1 && (interval == 2 || interval == 5))) score += 3; // third (or sus tone)
      if (interval == 10 || interval == 11 || (interval == 9 && strcmp(chord.suffix, "dim7") == 0)) {
        score += 2; // seventh (dim7: its bb7)
      }
    } else if (!(chord.optional & bit(interval))) {
      score -= 25; // a required tone is missing (harsher than the plan's -12: a name that claims a 7th
                   // that isn't played must lose against a plain chord with one extra note)
    }
  }
  score -= 15 * countPitchClasses(played & ~shape); // played notes outside the chord
  if (bass % 12 == root) score += 6;               // prefer root position readings
  if (chord.count > 3) score -= chord.count - 3;   // prefer simpler names
  return score;
}

/*! Position of the bass among the chord tones in ascending order, for triads and sevenths */
static int8_t inversionOf(const ExtendedChord &chord, uint8_t root, uint8_t bass)
{
  if (chord.count < 3 || chord.count > 4) return bass == root ? 0 : -1;
  uint8_t sorted[4];
  for (int i = 0; i < chord.count; i++) sorted[i] = chord.tones[i] % 12;
  for (int i = 1; i < chord.count; i++) { // insertion sort, at most 4 entries
    for (int j = i; j > 0 && sorted[j - 1] > sorted[j]; j--) {
      uint8_t t = sorted[j];
      sorted[j] = sorted[j - 1];
      sorted[j - 1] = t;
    }
  }
  uint8_t interval = (bass + 12 - root) % 12;
  for (int i = 0; i < chord.count; i++) {
    if (sorted[i] == interval) return i;
  }
  return -1;
}

int detectChords(uint16_t pcs, uint8_t bass, ScoredChord *out, int maxOut)
{
  int count = 0;
  for (int type = 0; type < kExtendedChordCount; type++) {
    for (int root = 0; root < 12; root++) {
      if (!(pcs & bit(root))) continue; // only roots that are played (no rootless readings)
      int score = scoreChord(pcs, bass, root, type);
      if (score < kMinScore) continue;
      ScoredChord candidate = {(uint8_t)root, (uint8_t)type, (uint8_t)(bass % 12),
                               inversionOf(kExtendedChords[type], root, bass % 12), (int16_t)score};
      // Insert sorted (best first; ties keep table order, i.e. the simpler name)
      int at = count < maxOut ? count : maxOut;
      while (at > 0 && out[at - 1].score < candidate.score) {
        if (at < maxOut) out[at] = out[at - 1];
        at--;
      }
      if (at < maxOut) {
        out[at] = candidate;
        if (count < maxOut) count++;
      }
    }
  }
  return count;
}

void leadSheetName(const ScoredChord &chord, char *out, size_t size, bool flats)
{
  const char *root = pitchClassName(chord.root, flats);
  const char *suffix = kExtendedChords[chord.type].suffix;
  if (chord.bass == chord.root) {
    snprintf(out, size, "%s%s", root, suffix);
  } else {
    snprintf(out, size, "%s%s/%s", root, suffix, pitchClassName(chord.bass, flats));
  }
}

KeyEstimate estimateKey(const float weights[12])
{
  // Krumhansl & Kessler key profiles
  static const float kMajor[12] = {6.35f, 2.23f, 3.48f, 2.33f, 4.38f, 4.09f, 2.52f, 5.19f, 2.39f, 3.66f, 2.29f, 2.88f};
  static const float kMinor[12] = {6.33f, 2.68f, 3.52f, 5.38f, 2.60f, 3.53f, 2.54f, 4.75f, 3.98f, 2.69f, 3.34f, 3.17f};
  float total = 0;
  for (int i = 0; i < 12; i++) total += weights[i];
  KeyEstimate best = {false, 0, false, -2};
  if (total <= 0) return best;
  for (int mode = 0; mode < 2; mode++) {
    const float *profile = mode ? kMinor : kMajor;
    float profileMean = 0, weightMean = total / 12;
    for (int i = 0; i < 12; i++) profileMean += profile[i];
    profileMean /= 12;
    for (int tonic = 0; tonic < 12; tonic++) {
      float num = 0, dw = 0, dp = 0;
      for (int i = 0; i < 12; i++) {
        float w = weights[(tonic + i) % 12] - weightMean;
        float p = profile[i] - profileMean;
        num += w * p;
        dw += w * w;
        dp += p * p;
      }
      if (dw <= 0) continue;
      float r = num / sqrtf(dw * dp);
      if (r > best.correlation) best = {true, (uint8_t)tonic, mode == 1, r};
    }
  }
  return best;
}
} // namespace MusicTheory
