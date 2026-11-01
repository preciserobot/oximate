# Stage 7: Ear trainer

> **Status: plan, not implemented.** Written 2026-10-31. Where this plan and the README's
> [Shared infrastructure](../../README.md#shared-infrastructure) section disagree, the README wins:
>
> * The trainer's default channel is **15**, not 16. Channel 16 is reserved for SAM2695 settings by stage 6. The trainer claims a synth part through `SamSynth`.
> * `Page::onMidi()` → register as an input observer.
> * `Leds::setOverride()`/`setPixel()` → use the shared LED claim API.
>
> **Decided 2026-11-01:**
>
> * Elektron gear on all channels will also play trainer notes (channel 15) in Separate mode. Recommend Bypass mode while training.

Stage 7 adds two pages:

* **Ear trainer**: OxiMate plays something on the SAM2695 and you identify
  it, either by tapping an answer on screen or by playing it back on a
  connected USB or DIN keyboard.
* **Chord explorer**: tap a root note and a chord type to hear the chord and
  see its notes.

Both are ordinary pages in the Ui page cycle. MIDI routing keeps running
unchanged while they're open.

## Exercises

| Exercise | Played | Answer by touch | Answer by keyboard |
|----------|--------|-----------------|--------------------|
| Intervals | two notes, rising, falling or together | interval name (m2 … P8) | play both notes |
| Chord quality | a triad or seventh chord, blocked or arpeggiated | quality (maj, min, dim …) | play the chord, any inversion |
| Scale degree | a key context (I–IV–V–I cadence), then one note | degree (1 … 7, with chromatic degrees at higher levels) | play the note |
| Melody echo | 3–6 notes in the current key | — | play them back in order |

* **Keyboard answers** compare pitch classes, so any octave counts. The
  interval exercise compares the *distance* between the two notes, so it can
  be played in any octave and starting from any note. The chord exercise
  checks the set of pitch classes. Melody echo checks the order of pitch
  classes and ignores timing.
* **Replay** (button A) plays the question again. **Skip** shows the answer
  and moves on, counted as wrong.
* **Root notes are random** within a playable range (C3–C5, MIDI 48–72). That
  way you learn the intervals themselves, not absolute pitches.

## Difficulty levels

| Level | Intervals | Chords | Scale degrees | Melody |
|-------|-----------|--------|---------------|--------|
| 1 | P4, P5, P8 | maj, min | 1, 3, 5 | 3 notes, steps only |
| 2 | + M2, M3, m3 | + dim, aug | + 2, 4, 6, 7 | 4 notes, up to a 3rd |
| 3 | all within an octave | + sus2, sus4, maj7, 7, m7 | + chromatic degrees | 5 notes |
| 4 | + falling and harmonic (played together) | + m7b5, dim7, inversions | random key per question | 6 notes, any interval |

* **Auto level**: after 10 answers in a row at 90% or better, offer the next
  level. Below 50%, offer the previous one. You choose whether to change.

## Ear trainer page

```
┌────────────────────────────────────────┐
│Ear>     USB 1C75:0288                  │  tap the header: next page
│ Intervals   Lvl 2     Streak 7  83%    │  exercise, level, streak, score
│                                        │
│        What interval did you hear?     │
│                                        │
│ [ m2 ][ M2 ][ m3 ][ M3 ][ P4 ][ TT ]   │  answer buttons (touch)
│ [ P5 ][ m6 ][ M6 ][ m7 ][ M7 ][ P8 ]   │  greyed = not in this level
│                                        │
│   ✔ Correct: M3 (C4 → E4)              │  result after answering
│ [Intervals][Chords][Degrees][Melody]   │  exercise selector
│   Replay       Next        Level 2     │  touch buttons A / B / C
└────────────────────────────────────────┘
```

* The answer grid changes with the exercise: 12 intervals, up to 12 chord
  qualities, or 7 to 12 scale degrees. Melody echo has no grid and shows
  "Play it back… 2/5" instead.
* After you answer, the correct button turns green. If you were wrong, your
  pick turns red. The result line spells out the notes, using the same note
  naming as the Monitor: MIDI 60 = C4, `noteName()` moved to a shared
  helper.
* **A** Replay, **B** Next question (Skip before you've answered), **C**
  cycles the level (1 → 4 → Auto → 1).

## Chord explorer page

```
┌────────────────────────────────────────┐
│Chords>  USB 1C75:0288                  │
│ [C ][C#][D ][D#][E ][F ]               │  root
│ [F#][G ][G#][A ][A#][B ]               │
│ [maj][min][dim][aug][sus2][sus4]       │  quality
│ [maj7][7 ][m7 ][m7b5][dim7][add9]      │
│                                        │
│ C maj7 :  C4  E4  G4  B4               │  notes of the chord
│ Intervals: R  M3  P5  M7               │
│ Inversion: root   Voicing: close       │
│   Play        Inversion    Arp/Block   │  touch buttons A / B / C
└────────────────────────────────────────┘
```

* Tapping a root or quality plays the chord immediately. **A** plays it
  again, **B** cycles the inversion (root, 1st, 2nd, 3rd), and **C** switches
  between blocked and arpeggiated playback.
* Chords played on a connected keyboard are *recognised* and shown, for
  example "You played: A min7 (1st inv)". This uses the same pitch-class
  matcher as the trainer, so the explorer doubles as a chord-naming tool.

## Sound output: SAM2695 only, or DIN OUT too?

The SAM2695 and DIN OUT share one wire (see "Unit MIDI: wiring"). In
Separate mode, everything the trainer plays also reaches external gear.

| Option | Pro | Con |
|--------|-----|-----|
| A. Play on a **reserved trainer channel** (default ch 16) | works in Separate mode; external gear set to other channels ignores it | uses up one channel; gear in omni mode will play along |
| B. Ask the user to flip the switch to **Bypass** while training | DIN OUT doesn't receive the trainer's notes at all | manual step; DIN OUT becomes a hardware thru meanwhile |
| C. Stop forwarding while the trainer is open | clean output | breaks the "routing always runs" rule; surprising |

* **Recommendation: A, with a "Trainer channel" setting** (default 16), plus a
  note in the README that gear in omni mode should use Bypass mode. The
  trainer sends a program change for its channel through `SamSynth` when the
  page opens (default GM 1, Acoustic Grand Piano), so the voice doesn't depend
  on what the external gear has selected.
* Sound goes out through **`SamSynth` (owned by stage 6)**: `noteOn`,
  `noteOff`, `program` and `allNotesOff(channel)`. The trainer never calls
  `DinMidi` directly. That keeps the "SAM and DIN OUT share a wire" logic in
  one place.
* Notes the trainer plays are registered with **`NoteTracker` (stage 16)**.
  Leaving the page or pressing panic then releases them cleanly.

## Timing and playback

* Playback needs only coarse timing: a note every 300–600 ms, gate about
  80%. **Decision: a small local scheduler** (`src/note_player.*`), not
  `ClockEngine`. Questions shouldn't depend on transport state, the
  external clock or swing.
* `NotePlayer` holds a queue of up to 32 `{atMillis, note, velocity, on/off}`
  events and runs from `poll()` in `loop()`. 1–2 ms of jitter is inaudible
  here. Once stage 8 moves MIDI handling to the timing task, `NotePlayer` can
  move to that task unchanged.
* The scale-degree cadence (I–IV–V–I) and arpeggios are just queued events.
  Replay cancels anything pending, then releases held notes with note-offs.

## Consuming input MIDI without disturbing routing

* `onMidi()` gains one line, after routing:
  `if (ui.currentPage()) ui.currentPage()->onMidi(msg);`. A default no-op
  `virtual void onMidi(const MidiMessage &)` is added to `Page`. Routing,
  LEDs and the Monitor are unchanged. The trainer only *observes* input.
* The trainer listens to **note-on** messages (velocity > 0) from any input
  and any channel. Answer channel is optional: "Any" or 1–16, saved in
  settings.
* **Chord detection**: collect note-ons into a set until 120 ms pass with no
  new note, or until all keys are released. Then match the pitch-class set
  against the chord table. The **melody echo** exercise takes notes one at a
  time.
* Input arriving while a question is still playing is ignored. That avoids
  matching the trainer's own notes if a keyboard echoes MIDI thru back.
* Whether you play along is up to you. By default your notes still reach the
  SAM2695 and DIN OUT through the router as usual, so you hear yourself.

## LED bar and vibration

* The trainer **takes over the LED bar** while its page is visible. This
  needs a small addition to `Leds`:
  * `setOverride(bool)`: while on, `activity()` flashes are suppressed.
  * `setPixel(index, color)`: set one LED directly.
  * `clear()`: turn all LEDs off.
* **Correct answer**: all 10 LEDs flash green, fading over about 400 ms.
  **Wrong answer**: red.
* **Streak meter**: between flashes, show the current streak, 1 LED per
  correct answer, up to 10. The colour moves from blue through cyan to white
  as the streak grows.
* **Chord explorer**: light the LEDs in the `noteColor()` of each chord tone,
  1–4 LEDs from the left.
* **Vibration** (optional setting, off by default): one short buzz (80 ms)
  for wrong, none for correct. Uses `M5.Power.setVibration()`, started and
  stopped from `poll()`.

## Scoring and stats

* For each exercise and level, `{asked, correct, bestStreak}` are stored as
  `uint16_t`, plus per-item confusion counts. For intervals, that's 12×12
  `uint8_t` (144 bytes) recording which interval was played versus which was
  answered.
* Stats are saved in NVS under namespace `oximate`, key `trainer`, as a
  versioned struct (well under 1 KB). They use the same delayed-save pattern
  as `Router`: written 2 s after the last change.
* **Weak-spot weighting** (level 3+): items with a lower hit rate are chosen
  more often (weight = 1 + misses / asked). That stops the trainer from
  repeating what you already know.
* A **stats view** replaces the answer grid while you hold button C for 1 s.
  It shows the hit rate per item as a bar and the top 3 confusions, e.g.
  "M6 heard as m6 ×4". "Reset stats" sits at the bottom with a confirmation
  tap.

## Code

| File | Purpose |
|------|---------|
| `src/music_theory.*` | interval/chord/scale tables, `noteName()` (moved from monitor.cpp), pitch-class set matching, chord naming incl. inversion |
| `src/note_player.*`  | small millisecond scheduler that plays notes via `SamSynth` and registers them with `NoteTracker` |
| `src/ear_trainer.*`  | exercise generation, answer checking, weighting, stats (no UI); unit-testable on the host |
| `src/trainer_page.*` | Ear trainer page |
| `src/chord_page.*`   | Chord explorer page |
| `src/ui.*`           | add `Page::onMidi()` and `Ui::currentPage()` |
| `src/leds.*`         | add `setOverride()`, `setPixel()`, `clear()` |

* `music_theory` and `ear_trainer` don't depend on Arduino, so they're tested
  on the Mac like the parsers in stages 1 and 2. Examples: "C E G B → C maj7
  root", "E G C → C maj 1st inv", weighting and level filters.
* Memory: tables are `constexpr` in flash. The page state is under 2 KB.

## Dependencies

* **Stage 6 `SamSynth`**: needed for sound. If stage 7 comes first, a minimal
  `SamSynth` with `noteOn`/`noteOff`/`program`/`allNotesOff` is written in
  stage 7 and extended by stage 6.
* **Stage 16 `NoteTracker`**: optional. Until it exists, `NotePlayer` keeps
  its own list of sounding notes and releases them when the page closes.

## Milestones

1. `music_theory` + host unit tests (intervals, chord tables, chord
   recognition with inversions).
2. `NotePlayer` + minimal `SamSynth`. A test button plays C maj7 on channel
   16, and you hear it on the Unit MIDI headphones.
3. `Page::onMidi()` and `Ui::currentPage()`. The chord explorer page,
   including recognising chords played on the keyboard.
4. Ear trainer: interval exercise with touch answers, level 1–4 filters,
   LED feedback (`Leds` override API).
5. Chord quality, scale degree (with cadence) and melody echo exercises.
   Keyboard answers.
6. Stats in NVS, weak-spot weighting, the stats view, vibration option.
7. README: mark stage 7 implemented and commit.

## Test plan

* **Host**: chord recognition for every quality in every inversion and all 12
  roots; interval classification including compound intervals folded to an
  octave; generators only produce items allowed at the current level;
  weighting distribution (10,000 draws, check the frequencies).
* **Device**:
  * Every exercise answered correctly and incorrectly, both by touch and by
    keyboard.
  * Replay while a question is still playing leaves no stuck notes.
  * Leaving the page mid-playback goes silent immediately.
* **Routing unaffected**: with the trainer open, keyboard notes still appear
  in the Monitor and reach DIN OUT. With external gear on channel 1, the
  trainer's channel-16 notes aren't heard on it.
* **Persistence**: stats and settings survive a power cycle. Changing levels
  rapidly causes only one NVS write.

## Risks and open questions

* **Omni-mode gear on DIN OUT will play the trainer's notes.** The answer is
  the Bypass workflow; we need to know whether your gear is affected.
* **SAM2695 sound quality**: GM piano is fine for intervals and chords, but
  some patches have slow attacks. The trainer should force a fast-attack
  program, and possibly offer a choice (piano, organ, sine-like flute).
* **Keyboards that echo MIDI thru** could feed the trainer's own notes back
  as answers. This is mitigated by ignoring input while a question plays.
* **Touch target size**: the 12-button answer grid uses about 50×26 px
  buttons, which is fine on the Core2 capacitive screen but tight. If not,
  intervals could be split across two pages.
* **Open**: Is channel 16 a good default trainer channel for your setup?
  Should the trainer answer only from a specific input or channel? Do you
  want fixed-do solfège (do re mi) or letter names? Do you want
  absolute-pitch exercises (name the note) too?
