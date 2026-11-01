#pragma once

#include <stddef.h>
#include <stdint.h>

/*! Note names, intervals, chords and scales (shared by stage 7 ear trainer and stage 21 chord
 *  detector). Pure C++ without Arduino, so it is unit-tested on the host.
 *
 *  Pitch-class sets are 12-bit masks: bit n = pitch class n (C = 0). Chord qualities are kept in
 *  one table so stage 21 can add extended and altered chords and a scoring matcher on top. */
namespace MusicTheory
{
constexpr int kMaxChordNotes = 6;

/*! Chord qualities, in table order (stage 21 appends more after Add9) */
enum ChordId : uint8_t
{
  Major,
  Minor,
  Diminished,
  Augmented,
  Sus2,
  Sus4,
  Major7,
  Dominant7,
  Minor7,
  HalfDiminished7,
  Diminished7,
  Add9,
  kChordCount,
};

struct ChordQuality
{
  const char *name;                       ///< e.g. "maj7"
  uint8_t intervals[kMaxChordNotes];      ///< semitones above the root, ascending, root first
  uint8_t count;
};

extern const ChordQuality kChordQualities[kChordCount];
/*! Interval names by semitones, 0 (P1) to 12 (P8) */
extern const char *const kIntervalNames[13];
/*! Scale-degree names by semitones above the tonic */
extern const char *const kDegreeNames[12];
/*! Semitones of the major scale's 7 degrees */
extern const uint8_t kMajorScale[7];

const char *pitchClassName(uint8_t pitchClass, bool flats = false);
/*! Note name with MIDI 60 = C4, e.g. "C#4" (or "Db4" with flats) */
void noteName(uint8_t note, char *out, size_t size, bool flats = false);

/*! Pitch-class set of a chord quality built on root 0 */
uint16_t chordMask(int quality);
/*! Pitch-class set transposed up by `semitones` */
uint16_t transpose(uint16_t mask, int semitones);
uint16_t pitchClassMask(const uint8_t *notes, int count);
int countPitchClasses(uint16_t mask);

/*! The chord's notes in close voicing, lowest first: inversion n moves the n lowest tones up an
 *  octave. Returns the note count (0 if the quality or inversion is invalid). */
int chordNotes(uint8_t rootNote, int quality, int inversion, uint8_t *out);

struct ChordMatch
{
  bool found;
  uint8_t root;      ///< pitch class
  uint8_t quality;   ///< ChordId
  uint8_t inversion; ///< 0 = root position, 1 = 1st, ...: which chord tone is in the bass
  uint8_t bass;      ///< pitch class of the lowest note
};

/*! Exact match of a pitch-class set against the chord table. When a set fits several roots
 *  (aug, dim7, sus2/sus4), the reading with the bass note as root wins. */
ChordMatch matchChord(uint16_t mask, uint8_t bassPitchClass);
/*! Match the notes being played (any octaves, any order) */
ChordMatch matchNotes(const uint8_t *notes, int count);

/*! "C maj7", or with the inversion "A min7 (1st inv)" */
void chordName(uint8_t root, int quality, char *out, size_t size, bool flats = false);
void chordName(const ChordMatch &match, char *out, size_t size, bool flats = false);
const char *inversionName(int inversion); ///< "root", "1st", "2nd", "3rd", ...

// ---------------------------------------------------------------------------------------------
// Stage 21: extended chords and a scoring matcher (chord detector). Lead-sheet names ("Cm7",
// "Cmaj7/E"), tolerant of missing optional tones and extra notes, ranked with alternatives.

constexpr int kMaxExtendedTones = 7;

struct ExtendedChord
{
  const char *suffix;                  ///< "" (major), "m", "maj7", "7sus4", "13b9", ...
  uint8_t tones[kMaxExtendedTones];    ///< semitones above the root (mod 12), root first
  uint8_t count;
  uint16_t optional;                   ///< interval mask of tones that may be left out (5th, 9th under 11/13)
};

extern const ExtendedChord kExtendedChords[];
extern const int kExtendedChordCount;

struct ScoredChord
{
  uint8_t root;     ///< pitch class
  uint8_t type;     ///< index into kExtendedChords
  uint8_t bass;     ///< pitch class of the lowest note
  int8_t inversion; ///< 0 = root position, 1 = 1st ...; -1 = no inversion label (bass not a chord tone, or extended chord)
  int16_t score;
};

constexpr int kMinScore = 20;         ///< weaker readings are not named
constexpr int kAlternativeWindow = 12; ///< alternatives must score within this of the best

/*! Interval mask (bit n = n semitones above the root) of an extended chord type */
uint16_t extendedMask(int type);
/*! Score one reading of a pitch-class set (see docs/stages/stage-21.md "Detection algorithm") */
int scoreChord(uint16_t pcs, uint8_t bass, uint8_t root, int type);
/*! All readings scoring at least kMinScore, best first; returns how many were written */
int detectChords(uint16_t pcs, uint8_t bass, ScoredChord *out, int maxOut);
/*! Lead-sheet name: "C", "Cm7", "Cmaj7/E", "C5" */
void leadSheetName(const ScoredChord &chord, char *out, size_t size, bool flats = false);

/*! Krumhansl-Schmuckler key estimate from pitch-class weights */
struct KeyEstimate
{
  bool found;
  uint8_t tonic; ///< pitch class
  bool minor;
  float correlation; ///< -1..1; the page treats < 0.6 as uncertain
};
KeyEstimate estimateKey(const float weights[12]);
} // namespace MusicTheory
