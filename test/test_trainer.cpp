// Stage 7: music theory, ear-trainer logic and SamSynth part claims
#include "test_support.h"

#include <Arduino.h> // fake millis()
#include <string.h>

#include "ear_trainer.h"
#include "music_theory.h"

using namespace MusicTheory;

TEST(testNoteNames)
{
  char name[8];
  noteName(60, name, sizeof(name));
  CHECK(strcmp(name, "C4") == 0);
  noteName(61, name, sizeof(name));
  CHECK(strcmp(name, "C#4") == 0);
  noteName(61, name, sizeof(name), true);
  CHECK(strcmp(name, "Db4") == 0);
  noteName(21, name, sizeof(name));
  CHECK(strcmp(name, "A0") == 0);
  noteName(0, name, sizeof(name));
  CHECK(strcmp(name, "C-1") == 0);
  CHECK(strcmp(kIntervalNames[7], "P5") == 0 && strcmp(kDegreeNames[10], "b7") == 0);
  CHECK(transpose(0x001, 13) == 0x002 && transpose(0x800, 1) == 0x001 && transpose(0x091, -12) == 0x091);
}

TEST(testChordNotes)
{
  uint8_t notes[kMaxChordNotes];
  CHECK(chordNotes(60, Major, 0, notes) == 3 && notes[0] == 60 && notes[1] == 64 && notes[2] == 67);
  CHECK(chordNotes(60, Major, 1, notes) == 3 && notes[0] == 64 && notes[1] == 67 && notes[2] == 72);
  CHECK(chordNotes(60, Major, 2, notes) == 3 && notes[0] == 67 && notes[1] == 72 && notes[2] == 76);
  CHECK(chordNotes(60, Dominant7, 3, notes) == 4 && notes[0] == 70 && notes[3] == 79);
  CHECK(chordNotes(60, Major, 3, notes) == 0); // a triad has no 3rd inversion
  CHECK(chordNotes(60, kChordCount, 0, notes) == 0);
}

// Every quality, every inversion, all 12 roots: the matcher names it back. The symmetric chords
// (aug, dim7) and the sus2 / sus4 pair are named from the bass, so for those only the
// pitch-class set and the bass have to agree.
TEST(testChordRecognitionAllRootsAndInversions)
{
  int failures = 0;
  for (int quality = 0; quality < kChordCount; quality++) {
    for (int root = 0; root < 12; root++) {
      for (int inversion = 0; inversion < kChordQualities[quality].count; inversion++) {
        uint8_t notes[kMaxChordNotes];
        int count = chordNotes(48 + root, quality, inversion, notes);
        ChordMatch match = matchNotes(notes, count);
        bool ambiguous = quality == Augmented || quality == Diminished7 || quality == Sus2 || quality == Sus4;
        bool ok = match.found && match.bass == notes[0] % 12;
        if (ambiguous) {
          ok = ok && transpose(chordMask(match.quality), match.root) == pitchClassMask(notes, count);
        } else {
          ok = ok && match.root == root && match.quality == quality && match.inversion == inversion;
        }
        if (!ok) {
          failures++;
          printf("  chord %s root %d inversion %d -> root %d %s inv %d\n", kChordQualities[quality].name, root,
                 inversion, match.root, kChordQualities[match.quality].name, match.inversion);
        }
      }
    }
  }
  CHECK(failures == 0);
}

TEST(testChordNamesAndAmbiguity)
{
  char name[32];
  uint8_t am7First[4] = {60, 64, 67, 69}; // C E G A: A min7 with C in the bass (no 6th chords yet)
  ChordMatch match = matchNotes(am7First, 4);
  chordName(match, name, sizeof(name));
  CHECK(strcmp(name, "A min7 (1st inv)") == 0);
  uint8_t csus2[3] = {60, 62, 67}, gsus4[3] = {55, 60, 62};
  CHECK(matchNotes(csus2, 3).root == 0 && matchNotes(csus2, 3).quality == Sus2);
  CHECK(matchNotes(gsus4, 3).root == 7 && matchNotes(gsus4, 3).quality == Sus4);
  uint8_t augOnE[3] = {64, 68, 72}; // E G# C: named from the bass
  CHECK(matchNotes(augOnE, 3).root == 4 && matchNotes(augOnE, 3).inversion == 0);
  uint8_t doubled[5] = {48, 60, 64, 67, 76}; // doubling and spread don't matter
  match = matchNotes(doubled, 5);
  CHECK(match.found && match.root == 0 && match.quality == Major && match.inversion == 0);
  uint8_t cluster[3] = {60, 61, 62};
  CHECK(!matchNotes(cluster, 3).found);
  chordName(matchNotes(cluster, 3), name, sizeof(name));
  CHECK(strcmp(name, "?") == 0);
  chordName(1, Minor7, name, sizeof(name), true);
  CHECK(strcmp(name, "Db min7") == 0);
}

TEST(testTrainerIntervals)
{
  EarTrainer trainer;
  trainer.begin(1234);
  trainer.setExercise(EarTrainer::Intervals);
  CHECK(trainer.answerCount() == 3); // level 1: P4, P5, P8
  for (int i = 0; i < 50; i++) {
    const EarTrainer::Question &q = trainer.next();
    CHECK(trainer.answerAvailable(q.answer));
    CHECK(q.notes[1] - q.notes[0] == q.answer && q.eventCount == 2);
  }
  // Keyboard: the distance counts, also falling and in another octave
  const EarTrainer::Question &q = trainer.next();
  int answer = q.answer;
  CHECK(trainer.noteOn(80) == EarTrainer::Pending);
  CHECK(trainer.noteOn(80 - answer - 12) == EarTrainer::Correct);
  // Touch: a wrong pick
  trainer.next();
  int8_t wrong = trainer.question().answer == 5 ? 7 : 5;
  CHECK(trainer.answerTouch(wrong) == EarTrainer::Wrong && trainer.picked() == wrong);
  CHECK(trainer.stats().streak == 0 && trainer.stats().total[EarTrainer::Intervals] == 2);
}

TEST(testTrainerChordsAnyInversion)
{
  EarTrainer trainer;
  trainer.begin(99);
  trainer.setExercise(EarTrainer::Chords);
  const EarTrainer::Question &q = trainer.next();
  uint16_t questionMask = pitchClassMask(q.notes, q.noteCount);
  // Play the same pitch classes in the last inversion, two octaves up
  ChordMatch match = matchNotes(q.notes, q.noteCount);
  uint8_t notes[kMaxChordNotes];
  int count = chordNotes(match.root + 72, match.quality, kChordQualities[match.quality].count - 1, notes);
  CHECK(pitchClassMask(notes, count) == questionMask);
  EarTrainer::Result result = EarTrainer::Pending;
  for (int i = 0; i < count; i++) result = trainer.noteOn(notes[i]);
  CHECK(result == EarTrainer::Correct);
  // A wrong chord is judged once enough pitch classes are held
  trainer.next();
  uint8_t cluster[3] = {60, 61, 62};
  result = EarTrainer::Pending;
  for (uint8_t n : cluster) result = trainer.noteOn(n);
  CHECK(result == EarTrainer::Wrong);
}

TEST(testTrainerDegreesAndMelody)
{
  EarTrainer trainer;
  trainer.begin(7);
  trainer.setExercise(EarTrainer::Degrees);
  const EarTrainer::Question &q = trainer.next();
  CHECK(q.key == 60 && q.eventCount == 13); // 4 cadence chords + the note
  CHECK(trainer.noteOn(q.key + q.answer + 24) == EarTrainer::Correct);

  trainer.setExercise(EarTrainer::Melody);
  for (int i = 0; i < 20; i++) {
    const EarTrainer::Question &m = trainer.next();
    CHECK(m.noteCount == 3 && m.eventCount == 3);
    for (int n = 1; n < m.noteCount; n++) {
      // level 1: steps only, i.e. neighbouring scale degrees
      int distance = abs(m.notes[n] - m.notes[n - 1]);
      CHECK(distance >= 1 && distance <= 2);
    }
  }
  const EarTrainer::Question &m = trainer.question();
  CHECK(trainer.noteOn(m.notes[0] + 12) == EarTrainer::Pending);
  CHECK(trainer.melodyProgress() == 1);
  CHECK(trainer.noteOn(m.notes[1]) == EarTrainer::Pending);
  CHECK(trainer.noteOn(m.notes[2] - 12) == EarTrainer::Correct);
}

TEST(testTrainerLevelsAndWeights)
{
  EarTrainer trainer;
  trainer.begin(42);
  trainer.setExercise(EarTrainer::Chords);
  trainer.cycleLevel();
  trainer.cycleLevel();
  trainer.cycleLevel();
  CHECK(trainer.level() == 4 && !trainer.autoLevel() && trainer.answerCount() == 11);
  trainer.cycleLevel();
  CHECK(trainer.autoLevel());
  // Auto: 10 right answers at level 4 stay at 4; 10 skipped ones step down
  for (int i = 0; i < 10; i++) {
    trainer.next();
    trainer.answerTouch(trainer.question().answer);
  }
  CHECK(trainer.level() == 4);
  for (int i = 0; i < 10; i++) {
    trainer.next();
    trainer.skip();
  }
  CHECK(trainer.level() == 3);
  trainer.cycleLevel(); // Auto -> 1
  CHECK(trainer.level() == 1 && !trainer.autoLevel());

  // Mistakes make an answer come up more often
  trainer.stats().mistakes[EarTrainer::Chords][MusicTheory::Minor] = 20;
  int minors = 0;
  for (int i = 0; i < 200; i++) minors += trainer.next().answer == MusicTheory::Minor;
  CHECK(minors > 150);
  char text[48];
  trainer.describe(text, sizeof(text));
  CHECK(strlen(text) > 5);
}

TEST(testSamSynthPartClaims)
{
  OutputRig rig;
  SamSynth synth;
  synth.begin(rig.outputs);
  gFakeMillis = 9000;
  synth.run(gFakeMillis);
  rig.dinBytes();

  // The trainer claims a part for channel 15: part 14, which already defaults to 15
  int8_t part = synth.claimPart(15);
  CHECK(part == 14);
  synth.setPart(part, 15, 0, 110);
  synth.run(gFakeMillis);
  std::vector<uint8_t> out = rig.dinBytes();
  CHECK(contains(out, gsBytes(0x40, 0x1E, 0x02, 15))); // to the config channel 16
  CHECK(contains(out, {0xCF, 0}) && contains(out, {0xBF, 7, 110}));
  CHECK(contains(out, gsBytes(0x40, 0x1E, 0x02, 14))); // then to channel 15
  CHECK(contains(out, gsBytes(0x40, 0x1F, 0x02, 16))); // part 15 (channel 16) muted meanwhile
  CHECK(contains(out, gsBytes(0x40, 0x1F, 0x02, 15))); // and back
  CHECK(synth.claimPart(15) == 5);                      // the next claim gets another part

  // A reset passing through to DIN OUT re-applies the claim
  rig.router.route(msg(MidiSource::Usb, {0xFF}));
  rig.dinBytes();
  gFakeMillis += 150;
  synth.run(gFakeMillis);
  CHECK(contains(rig.dinBytes(), gsBytes(0x40, 0x1E, 0x02, 14)));

  // Released in Layers mode: muted
  synth.config().listen = SynthListen::Layers;
  synth.changed();
  synth.releasePart(part);
  synth.run(gFakeMillis);
  CHECK(contains(rig.dinBytes(), gsBytes(0x40, 0x1E, 0x02, 16)));
}
