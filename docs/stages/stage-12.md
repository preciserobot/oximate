# Stage 12: Generative sequencers

> **Status: plan, not implemented.** Written 2026-11-01. Where this plan and the README's
> [Shared infrastructure](../../README.md#shared-infrastructure) section disagree, the README wins.

Pattern generators for live, dawless techno. They play alongside the drum machine (stage 5),
the M8 and the Elektron boxes, all on the same clock:

* **Euclidean tracks**: n hits spread as evenly as possible over k steps, rotated, per track.
  Polymetric lengths (e.g. 5 over 16 against 7 over 12) give evolving rhythms from a few
  numbers.
* **Turing tracks**: a looping melodic or rhythmic sequence that slowly mutates (after the
  "Turing Machine" Eurorack module). Locked, it repeats exactly; unlocked, it drifts a little
  every loop. The notes are quantised to a scale.
* **Mod lanes**: CC streams (sample & hold, smooth random, LFO shapes) for filter sweeps and
  other slow movement on an Elektron or M8 track.

## Where it lives: its own engine, plus a Euclidean fill in the drum machine

| Option | Verdict |
|--------|---------|
| Only inside the drum machine (Euclid per lane) | Too narrow: lanes share one channel and one 32-step grid, so no polymeter, no melodies, no CCs. |
| Only a separate engine | Then the drum machine can't use Euclid for its own patterns, which is the most-asked use. |
| **Both (chosen)** | A **Gen engine and page** for live, non-destructive generators (own lengths, channels, outputs), and a **"Euclid fill"** action in the drum machine's Pattern view that writes a Euclidean rhythm into a lane (normal, editable steps). Both use the same `euclid()` function. |

To avoid a second copy of the step and clock plumbing, the parts both engines need move into
a small shared helper:

* **`src/step_voice.*`** (new): sends a note-on through `MidiOutputs` with an origin, schedules
  its note-off with `ClockEngine::schedule()` after a gate length, and releases everything of
  its origin through `NoteTracker` on stop. Today this code sits in `DrumMachine::trigger()`,
  `noteOffCallback()` and `releaseAll()`; milestone 1 moves it out, and the drum machine then
  uses it too (no behaviour change, covered by the existing stage 5 tests).
* Both engines listen to the same `ClockEngine` Step events (16ths, swing applied) in the
  MIDI task; there is no second clock.

## Generators

The Gen engine has **8 slots**. Each slot is off or one of the three kinds below. Every slot
has these shared settings:

| Setting | Values | Notes |
|---------|--------|-------|
| Output | DIN, USB, both | |
| Channel | 1–16 | default: see open questions |
| Rate | 1/32, 1/16, 1/8, 1/4, 1/16T | per slot; 1/32 uses the clock's sub-step scheduler |
| Mute | on/off | B on the page, or tap the slot's mute chip |
| Probability | 0–100 % | per triggered step: skip the note with (100 − p) % chance |

### Euclidean

| Setting | Values |
|---------|--------|
| Steps (k) | 1–32 |
| Hits (n) | 0–k |
| Rotation | 0–k−1 |
| Note | 0–127 (drum note names shown on channel 10) |
| Velocity | 1–127, plus an **accent** Euclid (hits, rotation) that adds +27 velocity on its hits |
| Gate | 5–100 % of a step |

* `euclid(k, n, rotation)` returns a 32-bit mask using the Bresenham form (step i hits when
  `(i * n) % k < n`, then rotated), which gives the same patterns as Bjorklund's algorithm
  up to rotation. Pure C++, tested against the known tables (E(3,8) tresillo, E(5,8),
  E(7,16) …).
* Each slot loops over its own k steps, so different lengths drift against each other and
  against the drum machine's bar (polymeter). A **Reset** (hold A) realigns all slots to step 1
  on the next bar.

### Turing (mutating sequence)

A shift register of up to 16 values (0–255). Each step outputs the value at the head and
rotates the register by one. When a value comes round again it is **replaced by a new random
value with the mutation probability**.

| Setting | Values | Notes |
|---------|--------|-------|
| Length | 2–16 | loop length in steps |
| Mutate | 0–100 % | 0 = locked (repeats exactly), 100 = new random notes every loop |
| Scale | Chromatic, Major, Minor, Dorian, Phrygian, Harmonic minor, Minor pent., Major pent. | from `music_theory` |
| Root | C–B | |
| Base octave, range | octave 1–6, range 1–3 octaves | value 0–255 is scaled over the range, then quantised to the scale |
| Density | 0–100 % | values below the threshold are rests: rhythm from the same register |
| Gate | 5–100 %, plus "tie" at 100 % (legato into the next note) | |
| Velocity | fixed, or "from register" (a second register value) | |

* **Lock / unlock live** with B (mutation freezes at 0 % and returns to its setting), and
  **Reseed** by holding B (new random register). A "**Write**" action copies the current
  register into a fixed loop for later recall (one stored loop per slot).
* The random generator is a seeded `xorshift32` per slot, so tests are deterministic and a
  stored seed reproduces a loop exactly.
* `music_theory` gains scale masks and `quantise(note, rootPc, scaleMask)` (nearest scale note,
  ties go down); the stage 21 chord detector can reuse the scale table for its key estimate.

### Mod lane (CC)

| Setting | Values |
|---------|--------|
| CC number | 0–119 (default 74, cutoff) |
| Shape | S&H random, smooth random, sine, triangle, ramp up, ramp down, square |
| Rate | 1 step … 64 bars |
| Min / max | 0–127 each (max < min inverts) |
| Smoothing | sent on every clock tick (24 PPQN) for smooth shapes, on steps for S&H |

* Values are only sent when they change, which keeps DIN traffic low (a full-range sine over
  one bar at 120 BPM is under 200 bytes per second).
* Each mod lane remembers its last value; Stop leaves it where it is (no jump back).

## Gen page

```
┌────────────────────────────────────────┐
│Gen>     USB ok  INT 128.0 PLAY  ▮▮▮ 87%│
│ # Kind  Ch  Pattern            Mute    │
│ 1 EUC   10  x..x..x.x..x..x.   [  ]    │  live pattern, playhead inverted
│ 2 EUC   10  ..x...x...x...x.   [  ]    │
│ 3 TUR    3  C3 . D#3 G3 . C4   [  ]    │  Turing: next notes
│ 4 MOD    3  CC74 ▁▂▄▆█▆▄▂      [  ]    │  mod lane: recent values
│ 5 off                                  │
│ ...                                    │
│   -           Mute          +          │  touch buttons A / B / C
└────────────────────────────────────────┘
```

* **Slot list** (above): tap a row to select it, tap its Kind to cycle off / EUC / TUR / MOD,
  tap the row's pattern area to open the edit view.
* **Edit view** for the selected slot: a table of its settings in the Synth page style (tap a
  cell, A / C = − / +, holding repeats). Euclid shows a large ring of the k steps (dots on a
  circle, hits filled, playhead highlighted). Turing shows its register as bars, with the
  scale degree of each note.
* **Buttons** (consistent with the Synth page): **A** = −, **C** = +, **B** = the slot's live
  action: Mute on Euclid and Mod slots, **Lock/Unlock** on Turing slots; **hold B** = Reseed
  (Turing) or Randomise rotation (Euclid). In the slot list, A and C select the previous or
  next slot. **Hold A** = Reset (realign all slots on the next bar).
* Transport comes from the clock (Clock page, or a followed clock): generators play whenever
  the clock runs, unless muted. There is no separate Play button.

## Drum machine: Euclid fill

The drum page's Pattern view gets a **Fill** chip for the selected lane: steps (pattern
length), hits and rotation set with − / +, a live preview in the grid, and **Apply** writes the
steps (keeping accents where they land on hits). It's undoable once (the previous lane bits are
kept until the next edit). Implemented with the same `euclid()` function.

## LED bar

While the Gen page is visible it claims all 10 LEDs:

* **Euclid slot selected:** the 10 LEDs form the "ring". With k ≤ 10 each LED is a step (hits
  bright, rests dim, playhead white); with k > 10 the ring shows the 10 steps around the
  playhead.
* **Turing slot selected:** the current note in its `noteColor()`, a trail of the last notes
  fading, and LED 9 green (locked) or orange (mutating).
* **Mod slot selected:** a bar graph of the current value.
* Panic and warnings still overlay as everywhere.

## Engine, threading and persistence

* **`GenEngine`** runs in the MIDI task: a `ClockEngine` listener for Step and Tick events, and
  a loop hook for auditions. It owns the slots' playback state (positions, registers, mod
  phases, RNGs).
* **Settings** use the Router pattern: the UI edits `GenConfig` (its copy), `changed()` hands it
  to the task under a `CriticalSection`, and the task applies it at the next step. Live actions
  (mute, lock, reseed, reset) are small commands in the same handoff.
* **Status for the page** (positions, current notes, register contents, mod values) is copied
  into a snapshot under the lock each step; the page reads that at its redraw rate.
* **Persistence:** NVS key `gen` with a versioned `GenConfig` (8 slots × ~24 bytes) plus each
  Turing slot's stored loop and seed (~20 bytes), well under 1 KB. Saved 2 s after the last
  edit and only while stopped, as the drum machine does.
* **Stop** releases every note of origin `Origin::internal(12)` through `StepVoice` /
  `NoteTracker`. Mod lanes send nothing on stop.

## Files

| File | Change |
|------|--------|
| `src/step_voice.*` | new: note-on, gated note-off via the clock scheduler, release by origin |
| `src/drum_machine.*` | uses `StepVoice` (milestone 1, no behaviour change); Euclid fill on a lane |
| `src/drum_page.*` | Fill chip and preview in the Pattern view |
| `src/euclid.h` | new, pure: `euclid(k, n, rotation)` |
| `src/turing.*` | new, pure: register, mutation, density, quantised output; seeded `xorshift32` |
| `src/mod_lane.*` | new, pure: shapes, rate, min/max, change-only output |
| `src/music_theory.*` | scale table and `quantise()` |
| `src/gen_engine.*` | new: slots, clock listener, handoff, snapshot, NVS |
| `src/gen_page.*` | new: slot list, edit view, ring and register drawings, LEDs |
| `oximate.ino` | `GenEngine` global, `begin()` before the MIDI task starts, loop hook, page after Drums |
| `test/test_gen.cpp`, `test/sources_gen.mk` | host tests |

## Milestones

1. **`StepVoice`** extracted from the drum machine; stage 5 tests still pass.
2. **`euclid()`**, **Turing** and **mod lane** cores with host tests (deterministic seeds).
3. **`GenEngine`** with Euclid slots on the clock, handoff, snapshot, stop release.
4. **Gen page**: slot list, edit view, ring drawing, LEDs.
5. **Turing slots** (scale quantisation, lock/unlock, reseed, write) and **mod lanes**.
6. **Drum machine Euclid fill**; persistence; README and TESTING.md section; commit.

## Test plan

* **Host:**
  * `euclid()` against known patterns: E(3,8) = `x..x..x.`, E(5,8) = `x.xx.xx.` (up to
    rotation), E(4,16) four on the floor, n = 0 and n = k, rotation wraps.
  * Turing: with mutation 0 the output repeats exactly every `length` steps; with a fixed seed
    the mutated sequence is reproducible; at 100 % every value changes each loop; every output
    note is in the scale and the range; density 0 gives silence, 100 % never rests.
  * Mod lane: each shape's values at known phases, min/max inversion, change-only output count.
  * Engine (with the existing `OutputRig` and `runFor()` clock helpers): polymetric slots line
    up again after lcm(k1, k2) steps; probability 0 sends nothing; mute and stop release notes;
    gate lengths turn into scheduled note-offs; the Drum Euclid fill writes the right bits.
* **Hardware (TESTING.md section):** Euclid tracks against the drum machine at 120 and 140
  BPM, swing on; a Turing slot driving the M8 and an Elektron synth track (locked repeats,
  unlocked drifts, scale correct); a mod lane on CC74 into an Elektron filter, smooth with no
  zipper steps at slow rates; Diag page DIN TX drops stay at 0 with 8 busy slots plus drums.

## Risks and open questions

* **Elektron boxes listen on all channels** (see the decisions table): generators must use
  channels the Elektrons are set to, or ones they ignore; the channel defaults below are
  placeholders until answered.
* **DIN bandwidth:** 8 slots plus 8 drum lanes plus smooth CC lanes at fast tempos can approach
  DIN's ~3000 bytes/s. The Diag page's DIN drop counter shows it; smooth mod lanes can fall back
  to steps if drops appear.
* **CC conflicts:** a mod lane and a hardware knob on the same CC fight each other; the page
  shows the CC number prominently.
* **Open questions for the user:**
  1. Which channels and outputs should generators use by default (e.g. Euclid on the drum
     channel 10 to DIN, Turing on the M8's channel over USB or TRS, mod lanes on an Elektron
     track's channel)? Which channels do your Elektron tracks listen on?
  2. Which scales do you use most (minor, phrygian, dorian …)? Is a fixed root per slot enough,
     or should all Turing slots follow one global root/scale?
  3. Should Euclid slots also be able to drive the DFAM (stage 20) or a gate output later?
  4. Do you want "scenes" (snapshots of all slot settings) to switch during a set, or is one
     saved setup enough for now?
  5. Is a 1/16T (triplet) rate useful for you, or are straight divisions enough?
