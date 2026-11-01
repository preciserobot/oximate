# Stage 11: MIDI effects

> **Status: plan, not implemented.** Written 2026-11-01. Where this plan and the README's
> [Shared infrastructure](../../README.md#shared-infrastructure) section disagree, the README wins.

Effects that change what you play on the way through OxiMate: transpose,
scale quantiser, chord generator, arpeggiator, velocity curves, and CC
scaling and smoothing. They act on the merge path, so a USB keyboard or the
Oxi on DIN can be transposed, quantised, chorded or arpeggiated before the
notes reach DIN OUT, the SAM2695 and the USB device.

## Goals

* Live-friendly: each effect on or off with one tap, and settings that don't
  leave notes stuck when they change mid-note.
* Clock-tight arpeggiator: steps come from the stage 8 clock engine (internal
  clock or a followed M8/Elektron clock), so swing applies.
* No change for anything OxiMate generates itself (drum machine, synth
  auditions, ear trainer, generators): effects apply to **inputs** only.

## Where effects sit

```
 input ─► interceptors ─► Router: filter ─► force channel ─► FX chain ─► outputs
                                                (per input)       (DIN, USB)
```

* **Per input, after the router's filter and channel rewrite, before the
  outputs.** Each input (USB, DIN) has its own chain. Reasons:
  * One chain feeds every destination of that input (DIN OUT and USB), so a
    transposed USB keyboard sounds transposed everywhere.
  * Filtering first means blocked channels never reach the effects.
  * Forced channel first means the chord generator and arp see the channel
    that actually goes out.
* Messages the chain doesn't touch (clock never gets here: the clock engine
  owns it; SysEx, program change, pitch bend, aftertouch) pass unchanged.
* An effect can produce **0, 1 or many** messages per input message (chord: 1
  → 4 note-ons; arp: note-ons are held back and released on clock steps). The
  router hands the chain an emit callback that sends to the route's
  destinations with `Origin::fromInput(input)`, so the note tracker, the
  release-before-change logic and panic keep working unchanged.

## Effects and fixed order

A fixed order keeps the UI simple and the result predictable. Each slot is
simply on or off:

| # | Effect | Acts on | Settings |
|---|--------|---------|----------|
| 1 | **Transpose** | notes | −24 … +24 semitones, octave ±2 |
| 2 | **Scale quantise** | notes | root, scale (major, minor, dorian, phrygian, mixolydian, harmonic minor, pentatonic min/maj, chromatic), direction (nearest / up / down) |
| 3 | **Chord** | notes | chord quality from `music_theory` (maj, min, 7, min7, sus4, power 5th, octave), inversion, spread (close / open), or "diatonic": build the chord from the current scale |
| 4 | **Arpeggiator** | notes | rate (1/4, 1/8, 1/16, 1/32, triplets), mode (up, down, up-down, random, as played, chord), octaves 1–4, gate 10–100 %, latch, retrigger on new note |
| 5 | **Velocity** | note velocity | curve (linear, soft, hard, fixed value), min/max, random ± |
| 6 | **CC scale** | CC | source CC → target CC, min/max, invert, curve; per-CC smoothing (slew time 0–500 ms) |

Order rationale: transpose before quantise (so the transposed note snaps to
the scale), quantise before chord (so the chord root is in the scale), chord
before arp (so one key arpeggiates a whole chord), velocity last (it shapes
everything that comes out). `music_theory` gains the scale tables and a
`quantise(note, root, scale, direction)` helper; stage 12 needs the same one
(whichever stage is built first adds it).

## Note-off correctness

The main risk with note effects is a note-off that no longer matches the
note-on after a setting changed. Rule: **an input note remembers what it
produced.**

* Each chain keeps, per channel and input note, the list of output notes it
  sent (up to 6, for chords): `uint8_t produced[16][128][6]` would be 12 KB per
  input, so it is a 128-slot table `{channel, inNote, outNotes[6], count}`
  per input instead (about 1.3 KB), like the note tracker's slots.
* The note-off for an input note releases exactly its recorded output notes,
  whatever the settings are now.
* Settings changes are handed to the MIDI task like the router's (pending copy
  under a lock). Changing transpose, scale or chord affects the **next**
  note-on only.
* Turning an effect off, or switching presets, releases nothing by itself:
  held notes end normally through their recorded mapping. The arpeggiator is
  the exception (below).
* Velocity and CC effects are stateless per message apart from smoothing.

## Arpeggiator

* Runs on clock-engine `Step` events in the MIDI task (16ths, swing applied).
  Rates other than 1/16 are derived from the step and tick counts; 1/32 and
  triplets use `ClockEngine::schedule()` between steps.
* Held notes (after transpose, quantise and chord) go into a sorted note set;
  "as played" keeps the order.
* Each arp note gets its note-off after `gate × rate` via `schedule()`.
* **Clock stopped:** the arp can't step. Default: while stopped, held notes
  pass through unarpeggiated (you still hear something), and the arp starts on
  the next Start. Option: free-run on the internal tempo even when the
  transport is stopped.
* **Latch:** the note set stays after you release the keys; a new chord
  replaces it; tapping the arp slot off ends it.
* Turning the arp off or changing presets: the arp's sounding note gets its
  note-off immediately, and still-held keys start sounding normally.
* The arp's notes are tracked with the input's origin, so router changes and
  panic release them like any other note from that input.

## CC scaling and smoothing

* Scaling is a lookup table per mapping (256 bytes), rebuilt when settings
  change.
* Smoothing sends interpolated values at most every 10 ms via `schedule()`,
  and stops once the target is reached. DIN bandwidth: one CC at 100 Hz is
  300 bytes/s (about 10 % of DIN); the page warns when more than 3 smoothed CCs
  are active at once.
* Up to 8 mappings per input.

## FX page

```
┌────────────────────────────────────────┐
│FX>      USB ok  INT 120.0 PLAY  ▮ 87%  │
│ [ USB in ]  [ DIN in ]   Preset 3      │  input tabs, preset chip
│ [x] Transpose   +12                    │  tap the box: on/off
│ [x] Scale       A minor  nearest       │  tap the text: select
│ [ ] Chord       min7  inv 0  close     │
│ [x] Arp         1/16 up-down 2 oct 60% │
│ [ ] Velocity    soft  20-127           │
│ [ ] CC          74→74 smooth 80 ms     │
│                                        │
│   -           Bypass           +       │  touch buttons A / B / C
└────────────────────────────────────────┘
```

* Tap the check box to switch a slot on or off. Tap a slot's text to select
  it; the selection then steps through that slot's settings on repeated taps.
* **A** / **C**: decrease / increase the selected setting (hold repeats).
* **B**: bypass the whole chain of the shown input (the header shows `FX
  BYP`); hold B: save the current chains to the selected preset.
* Tapping the preset chip cycles presets 1–8 (loads immediately).
* LEDs while the page is open: one LED per active slot in its colour; the arp
  LED flashes on each arp step.

## Presets and settings

* `FxConfig` (versioned, NVS key `fx`): both input chains plus 8 presets of
  chains, about 1.5 KB.
* A preset holds both inputs' chains, so "preset 3" can mean "USB keyboard
  arpeggiated in A minor, Oxi transposed up an octave".
* Stage 15 (scenes) can recall a preset.

## Thread model

* All effect processing happens in the MIDI task, inside `Router::route()`
  and in the clock listener (arp) and scheduler callbacks (gates, smoothing).
* The UI edits its copy and calls `changed()`; the task picks the new
  settings up at the start of its next loop, as the router does.
* The FX page reads a small snapshot (held notes per input, arp position)
  for display.

## Files

| File | Change |
|------|--------|
| `src/fx_chain.*` (new) | effects, note mapping table, arp, CC smoothing; pure C++ where possible |
| `src/fx_page.*` (new) | FX page |
| `src/music_theory.*` | scale tables, `quantise()`, diatonic chord helper (shared with stage 12) |
| `src/router.*` | per-input chain between channel rewrite and outputs; emit callback |
| `oximate.ino` | create chains, register the page after Router |
| `test/test_fx.cpp`, `test/sources_fx.mk` | host tests |

## Milestones

1. `music_theory` scales and `quantise()`, transpose and quantise effects, the
   note mapping table, router integration. Tests.
2. Chord effect (incl. diatonic chords). Tests for note-offs after settings
   changes.
3. Arpeggiator on clock steps, gate scheduling, latch, stopped-clock
   behaviour.
4. Velocity curves, CC scaling and smoothing.
5. FX page, presets, NVS, LEDs.
6. README, TESTING.md section, hardware test with the Oxi and a USB keyboard.

## Host tests

* Transpose and quantise for every scale and direction; notes at the MIDI
  range edges (0, 127) clamp instead of wrapping.
* Chord: correct notes per quality and inversion; diatonic chords per degree.
* Note-off correctness: hold a note, change transpose / scale / chord, release
  it: exactly the original output notes get note-offs; no output note stays
  held in the note tracker.
* Arp: with a fake clock, each mode produces the expected sequence; gates end
  on time; latch keeps the set; turning the arp off releases its note.
* Velocity curves and CC tables; smoothing reaches the target and stops.
* Pass-through: SysEx, program change, bend, aftertouch unchanged.

## Risks

* **DIN bandwidth:** chords and fast arps multiply traffic; a 4-note chord at
  1/32 is still well under DIN's limit, but chords on top of CC smoothing can
  add up. The DIN queue's realtime priority keeps clock tight regardless.
* **Echo loops:** chords make more outgoing notes; the loop protection covers
  them because it records every sent channel message.
* **Arp and external sequencers:** arpeggiating the Oxi's sequence while the
  Oxi also sends clock works, but expect the arp to follow OxiMate's clock
  source setting, not the notes' timing.
* **Latch surprises live:** a latched arp keeps playing after hands leave the
  keys; the slot's LED and the header make it visible, and panic stops it.

## Open questions

1. Per input (USB, DIN) chains as proposed, or chains per channel (e.g. only
   the Oxi's track 1 on channel 1 gets the arp)?
2. Which effects matter most for your techno sets? (Proposed build order:
   transpose + quantise, arp, chord, velocity, CC.)
3. Arp while the transport is stopped: pass notes through (default) or
   free-run on the internal tempo?
4. Default scale and root, or none (chromatic) until set?
5. Which CCs would you scale or smooth (e.g. filter cutoff 74 from a USB
   controller to an Elektron)?
