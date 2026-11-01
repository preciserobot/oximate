// Stage 21: chord detector (extended matcher, held-note input, detector)
#include "test_support.h"

#include <Arduino.h>

#include "chord_detector.h"
#include "chord_input.h"
#include "music_theory.h"

using namespace MusicTheory;

/*! Best lead-sheet name for notes (the lowest note is the bass), "" if nothing is named */
static std::string nameOf(std::initializer_list<uint8_t> notes, bool flats = false)
{
  uint16_t pcs = 0;
  uint8_t bass = 255;
  for (uint8_t n : notes) {
    pcs |= 1 << (n % 12);
    if (n < bass) bass = n;
  }
  ScoredChord chords[8];
  int count = detectChords(pcs, bass % 12, chords, 8);
  if (!count) return "";
  char name[24];
  leadSheetName(chords[0], name, sizeof(name), flats);
  return name;
}

TEST(testExtendedChordNames)
{
  CHECK(nameOf({60, 64, 67}) == "C");
  CHECK(nameOf({60, 63, 67}) == "Cm");
  CHECK(nameOf({60, 67}) == "C5");
  CHECK(nameOf({64, 67, 72}) == "C/E");
  CHECK(nameOf({60, 64, 67, 71}) == "Cmaj7");
  CHECK(nameOf({64, 67, 71, 72}) == "Cmaj7/E");
  CHECK(nameOf({60, 64, 67, 70}) == "C7");
  CHECK(nameOf({60, 63, 67, 70}) == "Cm7");
  CHECK(nameOf({60, 63, 66, 70}) == "Cm7b5");
  CHECK(nameOf({60, 65, 67, 70}) == "C7sus4");
  CHECK(nameOf({60, 62, 64, 67}) == "Cadd9");
  CHECK(nameOf({60, 64, 67, 70, 74}) == "C9");
  CHECK(nameOf({60, 64, 70, 74}) == "C9");      // fifth left out
  CHECK(nameOf({60, 64, 70, 74, 81}) == "C13"); // 13 without 5th and 11th
  CHECK(nameOf({60, 64, 67, 70, 73}) == "C7b9");
  CHECK(nameOf({60, 63, 67, 70, 74}) == "Cm9");
  CHECK(nameOf({61, 65, 68}) == "C#");
  CHECK(nameOf({61, 65, 68}, true) == "Db");
  // The bass decides between C6 and Am7
  CHECK(nameOf({60, 64, 67, 69}) == "C6");
  CHECK(nameOf({57, 60, 64, 67}) == "Am7");
  // A bass note outside the chord stays as a slash
  CHECK(nameOf({54, 60, 64, 67}) == "C/F#");
  // A cluster matches nothing well enough
  CHECK(nameOf({60, 61, 62, 63, 64}) == "");
}

TEST(testEveryTriadAndSeventhInEveryInversion)
{
  const char *suffixes[] = {"", "m", "dim", "aug", "maj7", "7", "m7", "m7b5"};
  for (const char *suffix : suffixes) {
    int type = 0;
    while (type < kExtendedChordCount && strcmp(kExtendedChords[type].suffix, suffix) != 0) type++;
    CHECK(type < kExtendedChordCount);
    const ExtendedChord &chord = kExtendedChords[type];
    for (int root = 0; root < 12; root++) {
      for (int inversion = 0; inversion < chord.count; inversion++) {
        uint16_t pcs = transpose(extendedMask(type), root);
        uint8_t bass = (root + chord.tones[inversion]) % 12;
        ScoredChord chords[8];
        int count = detectChords(pcs, bass, chords, 8);
        bool symmetric = strcmp(suffix, "aug") == 0; // C+ = E+ = G#+: any of them is right
        bool ok = count > 0 && chords[0].type == type && (symmetric || chords[0].root == root);
        // The same notes named after the bass are conventional too: Cm7/Eb = Eb6, Cm7b5/Eb = Ebm6
        if (count > 0 && !ok) {
          bool sameNotes = transpose(extendedMask(chords[0].type), chords[0].root) == pcs;
          ok = sameNotes && chords[0].root == bass && chord.count == 4;
        }
        if (!ok) {
          printf("  %s root %d inv %d -> %s root %d\n", suffix, root, inversion,
                 count ? kExtendedChords[chords[0].type].suffix : "-", count ? chords[0].root : -1);
        }
        CHECK(ok);
      }
    }
  }
}

TEST(testKeyEstimate)
{
  float cMajor[12] = {};
  for (int pc : {0, 2, 4, 5, 7, 9, 11}) cMajor[pc] = 1;
  cMajor[0] += 1;
  cMajor[7] += 0.5f;
  KeyEstimate key = estimateKey(cMajor);
  CHECK(key.found && key.tonic == 0 && !key.minor);
  float aMinor[12] = {};
  for (int pc : {9, 11, 0, 2, 4, 5, 7}) aMinor[pc] = 1;
  aMinor[9] += 1.5f;
  aMinor[4] += 0.5f;
  key = estimateKey(aMinor);
  CHECK(key.found && key.tonic == 9 && key.minor);
  float none[12] = {};
  CHECK(!estimateKey(none).found);
}

static MidiMessage chordMsg(MidiSource source, std::initializer_list<uint8_t> bytes)
{
  static uint8_t buf[3];
  size_t n = 0;
  for (uint8_t b : bytes) buf[n++] = b;
  return {source, 0, buf, (uint16_t)n};
}

static bool hasNote(const NoteSet &set, uint8_t note) { return set.notes[note >> 5] & (1u << (note & 31)); }

TEST(testChordInputHeldNotes)
{
  ChordInput input;
  input.run(0, false, 0);
  input.observe(chordMsg(MidiSource::Usb, {0x90, 60, 100}), 10);
  input.observe(chordMsg(MidiSource::Din, {0x93, 64, 100}), 10);
  NoteSet set = input.snapshot();
  CHECK(set.count == 2 && set.lowest == 60 && hasNote(set, 64));
  input.observe(chordMsg(MidiSource::Usb, {0x90, 60, 0}), 20); // velocity 0 = note-off
  set = input.snapshot();
  CHECK(set.count == 1 && set.lowest == 64);
  // Drums (channel 10) are ignored by default
  input.observe(chordMsg(MidiSource::Usb, {0x99, 36, 100}), 30);
  CHECK(input.snapshot().count == 1);
  // All notes off clears that channel
  input.observe(chordMsg(MidiSource::Din, {0xB3, 123, 0}), 40);
  CHECK(input.snapshot().count == 0);

  // Filter: USB only, channel 1
  ChordFilter filter;
  filter.inputs = 1;
  filter.channel = 1;
  input.setFilter(filter);
  input.run(50, false, 0);
  input.observe(chordMsg(MidiSource::Usb, {0x90, 60, 100}), 60);
  input.observe(chordMsg(MidiSource::Usb, {0x91, 62, 100}), 60);
  input.observe(chordMsg(MidiSource::Din, {0x90, 64, 100}), 60);
  set = input.snapshot();
  CHECK(set.count == 1 && set.lowest == 60);

  // Unplugging the USB device clears its notes
  input.run(70, true, 0);
  input.run(80, false, 0);
  CHECK(input.snapshot().count == 0);
}

TEST(testChordInputSustainAndArpeggio)
{
  ChordInput input;
  ChordFilter filter;
  filter.sustain = true;
  input.setFilter(filter);
  input.run(0, false, 0);
  input.observe(chordMsg(MidiSource::Usb, {0xB0, 64, 127}), 0);
  input.observe(chordMsg(MidiSource::Usb, {0x90, 60, 100}), 0);
  input.observe(chordMsg(MidiSource::Usb, {0x80, 60, 0}), 5);
  CHECK(input.snapshot().count == 1); // still sounding under the pedal
  input.observe(chordMsg(MidiSource::Usb, {0xB0, 64, 0}), 10);
  CHECK(input.snapshot().count == 0);

  // Arpeggio window: notes stay for 250 ms after their note-off
  filter.sustain = false;
  filter.arpMillis = 250;
  input.setFilter(filter);
  input.run(100, false, 0);
  for (uint8_t note : {60, 64, 67}) {
    input.observe(chordMsg(MidiSource::Usb, {0x90, note, 100}), 100);
    input.observe(chordMsg(MidiSource::Usb, {0x80, note, 0}), 150);
  }
  CHECK(input.snapshot().count == 3);
  input.run(450, false, 0);
  CHECK(input.snapshot().count == 0);

  // A completed panic clears everything
  input.observe(chordMsg(MidiSource::Usb, {0x90, 61, 100}), 500);
  input.run(510, false, 1);
  CHECK(input.snapshot().count == 0);
}

TEST(testChordDetector)
{
  ChordDetector detector;
  NoteSet set = {{0, 0, 0, 0}, 0xFF, 0, 1};
  auto play = [&](std::vector<uint8_t> notes) {
    memset(set.notes, 0, sizeof(set.notes));
    set.lowest = 0xFF;
    set.count = 0;
    for (uint8_t n : notes) {
      set.notes[n >> 5] |= 1u << (n & 31);
      if (n < set.lowest) set.lowest = n;
      set.count++;
    }
    set.version++;
  };
  char name[24];

  play({60, 64, 67});
  detector.update(set, 1000);
  CHECK(detector.settling());
  detector.update(set, 1050);
  CHECK(!detector.settling() && detector.result().kind == ChordDetector::Kind::Chord);
  detector.stableName(name, sizeof(name));
  CHECK(std::string(name) == "C");
  CHECK(detector.historyCount() == 0); // not yet stable for 150 ms
  detector.update(set, 1250);
  CHECK(detector.historyCount() == 1);

  // Alternatives: C6 shows Am7/C as an alternative
  play({60, 64, 67, 69});
  detector.update(set, 2000);
  detector.update(set, 2050);
  const ChordDetector::Result &r = detector.result();
  CHECK(r.kind == ChordDetector::Kind::Chord && r.alternativeCount >= 1);
  char alt[24] = "";
  if (r.alternativeCount) leadSheetName(r.alternatives[0], alt, sizeof(alt));
  CHECK(std::string(alt) == "Am7/C");

  // Two notes: interval, or a power chord
  play({60, 64});
  detector.update(set, 3000);
  detector.update(set, 3050);
  CHECK(detector.result().kind == ChordDetector::Kind::Interval && detector.result().interval == 4);
  play({60, 67});
  detector.update(set, 4000);
  detector.update(set, 4050);
  detector.stableName(name, sizeof(name));
  CHECK(std::string(name) == "C5");

  // A progression in C major: history keeps the last 4, the key estimate says C major
  uint32_t t = 5000;
  for (const std::vector<uint8_t> &chord : std::vector<std::vector<uint8_t>>{
           {57, 60, 64}, {62, 65, 69}, {55, 59, 62, 65}, {60, 64, 67}}) {
    play(chord);
    detector.update(set, t);
    detector.update(set, t + 50);
    detector.update(set, t + 250);
    t += 1000;
  }
  CHECK(detector.historyCount() == 4);
  leadSheetName(detector.history(3), name, sizeof(name));
  CHECK(std::string(name) == "C");
  KeyEstimate key = detector.key();
  CHECK(key.found && key.tonic == 0 && !key.minor);

  // Frozen: changes are ignored
  detector.setFrozen(true);
  play({62, 66, 69});
  detector.update(set, 20000);
  detector.update(set, 20100);
  detector.stableName(name, sizeof(name));
  CHECK(std::string(name) == "C");
}
