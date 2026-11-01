# Stage 21: Chord detector

> **Status: plan, not implemented.** Written 2026-11-01. Where this plan and the README's
> [Shared infrastructure](../../README.md#shared-infrastructure) section disagree, the README wins.
> Shares `src/music_theory.*` with stage 7: whichever of the two is built first creates it.
>
> **Decided 2026-11-01:**
>
> * Note names use **sharps** by default (C toggles flats).
> * **Channel 10 (drums) is excluded** from detection by default.
> * The full page (4-chord history, key estimate) is the default; **B toggles
>   a simple view** that shows only the current chord, large. Freezing the
>   display moves to holding A.
> * The chord name is **also shown on the Oxi One's screen** once stage 3's
>   remote mode is running (see "Chord on the Oxi screen").

A page that names the chord currently being played into OxiMate, from a USB
keyboard, the Oxi One, the M8 or an Elektron sequencer.

## Goal and use cases

* **Live:** play on a keyboard and see the chord name, its notes and the bass
  note at a glance (`Cmaj7/E`).
* **Sequencer check:** see what chord the Oxi or the M8 is actually playing,
  including arpeggiated parts.
* **Learning:** see the name of a voicing you found by ear, plus the other
  names it could have (`C6` = `Am7/C`).
* The detector only **observes** input. Routing, the Monitor and the outputs
  are unchanged, and nothing is ever sent.

## Where the held notes come from

The page needs the set of notes currently **held at the inputs**. Two options
were considered:

| Option | Pro | Con |
|--------|-----|-----|
| UI side, from the `MidiEvent` ring in `loop()` | no change in the MIDI task | the ring drops events when full (counted on the Diag page); a lost note-off would leave a note "held" on screen |
| **MIDI task, as an input observer** | sees every message, exact held state, also while the page is hidden | a little code in the task |

**Chosen: an input observer in the MIDI task** (`src/chord_input.*`). It
keeps the exact held set and publishes a small snapshot. The chord *matching*
and all text work happen on the UI side when the snapshot changes, so the task
stays light (bit operations only, no strings).

* `onMidi()` in `oximate.ino` gains one call after routing:
  `chordInput.observe(msg)`. This is the first user of the "input observers"
  piece in Shared infrastructure; later observers (stage 9) use the same spot.
* `NoteTracker` is **not** reused: it tracks what was *sent* per output, with
  origins and ages. The detector needs what was *received*, per input, and
  only bitsets.

### Held-note rules (`ChordInput`)

| Input | Effect |
|-------|--------|
| note-on, velocity > 0 | note held |
| note-off, or note-on velocity 0 | note released (or kept as sustained, see below) |
| CC64 ≥ 64 / < 64 | pedal down / up, per input and channel; only used when "Sustain" is on |
| CC123 or CC120 | clear that input's channel |
| USB device unplugged | clear the USB input |
| Panic (any level) | clear everything |

* State: `held[2][16][4]` and `sustained[2][16][4]` (`uint32_t`, 128 bits per
  channel per input) = 1 KB, plus a pedal mask per input.
* **Filter** (from settings): inputs USB / DIN / both, channel all or 1–16.
  Only notes passing the filter go into the published set.
* **Snapshot** (published under a `CriticalSection`, read by the UI):

  ```cpp
  struct NoteSet {
    uint32_t notes[4];    // 128-bit set: held (+ sustained) + arpeggio window
    uint8_t  lowest;      // lowest note in the set, 0xFF = empty
    uint8_t  count;
    uint32_t version;     // increments on every change
    uint32_t changedMillis;
  };
  ```

### Arpeggios and strums

Sequencers often play a chord one note at a time, and strummed or rolled
chords arrive over tens of milliseconds.

* **Strum settle** (always on, 40 ms): the UI waits until the set has been
  stable for 40 ms before naming it, so a rolled chord doesn't flash through
  partial names.
* **Arpeggio window** (setting: off / 250 / 500 / 1000 ms / 1 bar): notes
  stay in the set for that long after their note-off. "1 bar" uses the clock
  engine's tempo (snapshot BPM and beats per bar). This turns a C–E–G–C
  arpeggio into "C" instead of single notes. Implemented in the task as a
  per-note release timestamp (`uint32_t lastOff[128]`, 512 bytes), pruned in
  the task loop hook.
* **Freeze**: holding A freezes the last named chord until A is held again,
  so a short chord can be read after it ends.

## Detection algorithm (`src/music_theory.*`)

Pure C++, no Arduino, so it runs in the host tests.

1. **Pitch-class set:** fold the 128-bit note set into 12 bits (`pcs`). The
   bass is the lowest note's pitch class.
2. **Fewer than 2 pitch classes:** show the single note (or nothing).
   Exactly 2: show an interval or a power chord (`C5`), see the table.
3. **Candidates:** for every root `r` (0–11) and every chord in the table,
   compare the chord's interval mask rotated to `r` with `pcs`.
4. **Score** each candidate:

   | Term | Score |
   |------|-------|
   | each chord tone present | +10 (root +4 extra, third +3, seventh +2) |
   | each required tone missing | −12; the root and third are required unless the chord type says otherwise (`5`, `sus2`, `sus4`) |
   | optional tone missing (the 5th, the 9th under an 11th or 13th) | 0 |
   | each played note not in the chord | −15 |
   | root equals the bass | +6 (prefers root position, `C6` over `Am7/C` when C is lowest) |
   | chord "complexity" | −1 per extension (prefers `C` over `Cadd9no3`-style readings) |

5. **Result:** the best candidate is shown big. The next two (score within
   12 points of the best) are listed as alternatives. A candidate needs a
   score ≥ 20 to be shown at all. Otherwise the notes are listed with "?".
6. **Name:** root name (sharps or flats, setting), chord suffix, and
   `/bass` when the bass isn't the root (slash chord). If the bass isn't a
   chord tone at all, it's still shown as a slash chord (`C/F#`) rather than
   dropped, since a bass note under a chord is common.
7. **Inversion** label for triads and sevenths: root position, 1st, 2nd,
   3rd inversion (from the bass's chord degree).

### Chord table

The table holds interval masks (bit n = n semitones above the root),
required/optional bits and a suffix, as `constexpr` data in flash. Stage 7
creates the table with triads, sus chords and sevenths; stage 21 adds the
rest.

| Group | Suffixes | Added by |
|-------|----------|----------|
| Power, triads | `5`, (maj), `m`, `dim`, `aug` | 7 |
| Suspended | `sus2`, `sus4`, `7sus4` | 7 (`7sus4`: 21) |
| Sixths | `6`, `m6`, `6/9` | 21 |
| Sevenths | `maj7`, `7`, `m7`, `m(maj7)`, `m7b5`, `dim7`, `aug7`, `maj7#5` | 7 (`aug7`, `maj7#5`: 21) |
| Added tones | `add9`, `madd9`, `add11` | 21 |
| Ninths | `maj9`, `9`, `m9`, `7b9`, `7#9` | 21 |
| Elevenths | `11`, `m11`, `7#11`, `maj7#11` | 21 |
| Thirteenths | `maj13`, `13`, `m13`, `7b13`, `13b9` | 21 |
| Altered | `7alt` (b9/#9/b5/#5 shown as their own names first; `alt` only when ≥ 2 alterations) | 21 |

About 45 chord types × 12 roots = 540 comparisons per change, each a rotate,
an AND and two popcounts. Well under a millisecond on the UI core.

### What stage 21 adds to `music_theory`

* The extended/altered rows above.
* **Required/optional tone bits** per chord type and the scoring function
  (stage 7 only needs exact matching for its exercises; it switches to the
  scored matcher too, so both pages name chords the same way).
* `ChordMatch` results with score, inversion and slash bass, and `top(n)`.
* `chordName(match, flats, out)` with slash notation.
* Key estimate (optional milestone): Krumhansl–Schmuckler correlation of the
  last few chords' pitch-class weights against major/minor key profiles.

## Page

```
┌────────────────────────────────────────┐
│Chord>   USB 1C75:0288   INT 120.0 PLAY │
│                                        │
│            Cmaj7/E                     │  big name (FreeSansBold24)
│            1st inversion               │
│ Notes  E3 G3 B3 C4                     │  held notes, lowest first
│ Also   Em(b6)  Em#5                    │  up to 2 alternatives
│ ────────────────────────────────────── │
│ Am7   Dm7   G7    Cmaj7/E              │  history: last 4 chords
│ Key    C major (approx.)               │  optional key estimate
│ In USB+DIN ch-10    Arp off   Sus off  │  settings chips (tap)
│   Clear      View:full     Notes:#     │  touch buttons A / B / C
└────────────────────────────────────────┘
```

Simple view (B):

```
┌────────────────────────────────────────┐
│Chord>   USB 1C75:0288   INT 120.0 PLAY │
│                                        │
│                                        │
│              Cmaj7/E                   │  current chord only, as large
│                                        │  as fits (FreeSansBold24 at 2x)
│                                        │
│                                        │
│   Clear     View:simple    Notes:#     │
└────────────────────────────────────────┘
```

* **Big name:** grey while the set is settling, white when stable, cyan
  while frozen (hold A). When the notes match nothing well: the note names with a
  `?`.
* **Notes:** played notes, lowest first, colour-coded by pitch class
  (`noteColor()`), so they match the LEDs.
* **Alternatives:** other good readings, e.g. `C6` vs `Am7/C`.
* **History:** the last 4 distinct chords (a chord counts once it has been
  stable for 150 ms), handy for reading a sequencer's progression.
* **Settings chips** (bottom row, tap to cycle):
  * input: `USB+DIN`, `USB`, `DIN`
  * channel: `ch-10` (all except drums, the default), `all ch`, `ch 1` …
    `ch 16`
  * arpeggio window: `Arp off`, `250`, `500`, `1000`, `1 bar`
  * sustain pedal: `Sus off` / `Sus on`
* Buttons: **A** clears the history (and the held set, as a manual unstick);
  holding A freezes or releases the display. **B** switches between the full
  and the simple view (`View:full` / `View:simple`). **C** toggles
  sharps/flats (`Notes:#` / `Notes:b`).
* Redraws only when the snapshot version or the chord changes, at most
  every 50 ms; the name and notes rows are redrawn without clearing the page
  (no flicker), like the Clock page digits.

## LED bar

While the page is visible it claims all 10 LEDs:

| LEDs | Shows |
|------|-------|
| 0–4  | the chord's tones, root first, each in `noteColor()` of its pitch class (up to 5 tones) |
| 5–8  | extensions beyond 5 tones, else off |
| 9    | match quality: green = good, yellow = ambiguous (alternatives within 12 points), red = no match |

Panic and other warnings still overlay the bar, as everywhere.

## Settings (NVS)

Key `chord`, versioned struct, saved with the usual 2 s delay:

```cpp
struct ChordConfig {
  static constexpr uint8_t kVersion = 1;
  uint8_t version = kVersion;
  uint8_t inputs = 3;      // bit 0 USB, bit 1 DIN
  uint8_t channel = 0;     // 0 = all, 1-16
  bool skipDrums = true;   // with channel 0: ignore channel 10
  uint16_t arpMillis = 0;  // 0 = off, 0xFFFF = 1 bar
  bool sustain = false;
  bool flats = false;
  bool keyEstimate = true;
  bool simpleView = false; // B: only the current chord, large
};
```

The UI edits its copy and hands it to `ChordInput` the same way as the router
(`changed()` → pending copy under a lock → applied in the task).

## Files

| File | Change |
|------|--------|
| `src/music_theory.*` | new (or extended, if stage 7 came first): chord table, scoring matcher, names, key estimate |
| `src/chord_input.*` | new: input observer in the MIDI task, held/sustained bitsets, arpeggio window, `NoteSet` snapshot, `ChordConfig` hand-over and NVS |
| `src/chord_page.*` | new: the Chord page (stage 7's chord explorer is a separate page; if both exist they share `music_theory` and the name rendering) |
| `src/monitor.cpp` | `noteName()` moves to `music_theory` (if stage 7 hasn't done it) |
| `oximate.ino` | `chordInput.observe(msg)` in `onMidi()`, `chordInput.run(now)` in the task loop hook, add the page |
| `src/panic.*` | on panic, also `chordInput.clear()` (or the page's A button covers it) |
| `Makefile`, `test/test_main.cpp` | add `music_theory.cpp` and `chord_input.cpp` to the host tests |

Memory: ~1.7 KB task state, tables in flash, page state < 1 KB.

## Milestones

1. **`music_theory`** chord table, scoring matcher and naming, with host tests
   (see below). Shared with stage 7.
2. **`ChordInput`** in the MIDI task: held/sustained sets, filter, clear on
   USB unplug and panic, snapshot. Host tests.
3. **Chord page**: big name, notes, alternatives, A/B/C, settings chips,
   LED bar. First hardware test with a keyboard.
4. **Arpeggio window** and strum settle; test with the Oxi and M8 playing
   arpeggios.
5. **History and key estimate**; NVS settings; README and TESTING.md update,
   commit.

## Test plan

**Host (`make test`):**

* Every chord type in the table, all 12 roots, all inversions: the top match
  is the expected name (generated from the table itself, plus a hand-written
  list of tricky cases).
* Ambiguity: `C E G A` with C in the bass → `C6`, alternative `Am7/C`;
  with A in the bass → `Am7`.
* Missing fifth: `C E Bb` → `C7`; `C E G D` → `Cadd9`; `C E G Bb D` → `C9`.
* Extra note: `C E G F#` → `C(#11)`-style reading or `C/F#` depending on the
  bass; never a wrong root with a high score.
* Two notes: `C G` → `C5`; `C E` → interval name, no chord.
* Slash chords: `E` in the bass under `C E G` → `C/E`, 1st inversion; `D` in
  the bass under `C E G` → `C/D`.
* Flats setting: `Bb` vs `A#` naming.
* `ChordInput`: velocity-0 note-off, sustain on/off, CC123 clear, channel and
  input filters, USB unplug clear, arpeggio window expiry with a fake clock.

**Hardware:**

* USB keyboard: triads and sevenths in all inversions, a few 9th/13th
  voicings; check the alternatives look sensible.
* Oxi One and M8 playing chords and arpeggios over DIN, with the arpeggio
  window off and on.
* Sustain pedal on and off.
* Unplug the keyboard while holding a chord: the display clears.

## Chord on the Oxi screen

Once stage 3 drives the Oxi One in remote mode, the current chord name is
also written to the Oxi's OLED:

* Stage 3's `OxiRemote` gets a small text API (e.g. `setStatusText(line,
  text)`), and the Chord page (or a background hook, so it works on any
  page) calls it when the stable chord changes, at most a few times per
  second.
* The chord shares the screen with stage 3's own display; the exact line and
  font follow the Oxi remote protocol's display commands (stage 3, M0
  verifies them).
* Works only while the Oxi is the USB device in remote mode; with the Oxi on
  DIN there is no screen access.

## Risks and open questions

* **Naming conventions differ** (`C6` vs `Am7/C`, `Cm7b5` vs `Cø`). The
  scoring weights will need tuning against real playing; the alternatives
  row keeps the display honest.
* **Arpeggio window vs. melody:** with a long window, melodic notes over a
  chord get folded into it. "Off" is the default for that reason.
* **Many inputs at once** (drums on channel 10 from the Elektron while
  playing chords) pollute the set. Channel 10 is excluded by default, and the
  channel filter handles the rest.
* The user's answers (2026-11-01) are in the status note at the top.
