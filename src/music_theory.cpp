#include "music_theory.h"

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
} // namespace MusicTheory
