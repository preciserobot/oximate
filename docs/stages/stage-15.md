# Stage 15: Setlist / scenes

> **Status: implemented 2026-11-02** (see the README's stage 15 "Implemented" note for the
> differences from this plan). Written 2026-11-01. Where this plan and the README's
> [Shared infrastructure](../../README.md#shared-infrastructure) section disagree, the README wins.

One touch to move a live dawless set to its next part: a **scene** sends
program changes and CC snapshots to your gear and recalls OxiMate's own
settings (tempo, routing, synth layers, drum pattern, FX preset), optionally
on the next bar or phrase so it lands in time. A **setlist** is the ordered
list of scenes you step through during the set.

## Goals

* Live first: a big "current / next" view, one button to go to the next
  scene, and a visible countdown when the change is quantised.
* Safe with Elektron boxes: program changes switch their patterns, so they're
  only sent where you explicitly allow it, and never by accident.
* Tight timing: the clock never stutters while a scene sends its burst.

## What a scene contains

| Part | Content | Notes |
|------|---------|-------|
| Device messages | per device: program change (optional bank MSB/LSB), up to 16 CC values, optional short SysEx (≤ 32 bytes) | sent on the device's output and channel |
| Tempo | internal BPM, optional | ignored while following an external clock |
| Router | a stored `RouterConfig`, optional | uses the router's release-before-change |
| Synth | a stored `SynthConfig` (layers, effects), optional | uses SamSynth's diffing, so only changes are sent |
| Drums | a pattern number to queue, optional | the drum machine switches at its pattern end |
| FX | an FX preset number (stage 11), optional | |
| Generators | a stage 12 scene/preset, optional | if stage 12 exists |
| Timing | now / next bar / next phrase (stage 22) / next drum pattern end | |

"Optional" parts are simply not touched when unset, so a scene can be as small
as "program change 5 to the Digitakt".

## Devices

A short device list (up to 8), set up once:

| Field | Example |
|-------|---------|
| Name | `Digitakt` |
| Output | DIN or USB |
| Channel | 10 (its auto or program-change channel) |
| Program changes allowed | off by default |
| PC numbering shown as | 1–128 or 0–127, to match the device's display |
| Lead time | send PCs this many beats before the boundary (default 0) |

* **Elektron safety:** "Program changes allowed" is off for every new device.
  A scene can't contain a PC for a device unless it's allowed, and the scene
  editor shows a warning icon on such devices. Elektron boxes queue a received
  pattern change for the end of the current pattern (per their own setting),
  so sending the PC one beat early (lead time 1) makes the change land
  exactly on the boundary.
* The SAM2695 isn't a device here: its layers come with the stored
  `SynthConfig` (which goes out on its config channel).

## Recall and timing

* Recall happens in the MIDI task, so it can hit a boundary exactly. The UI
  hands over "recall scene n at <boundary>" through a small command queue.
* Boundaries come from the clock engine: `Bar` events, stage 22's phrase
  start, or the drum machine's pattern end. When the transport is stopped,
  quantised recalls happen immediately.
* **Burst order** at the boundary (all through `MidiOutputs` with
  `Origin::internal(15)`):
  1. PCs with lead time were already sent earlier.
  2. Tempo change (takes effect from the next clock tick).
  3. PCs without lead time, then bank selects first where set.
  4. OxiMate settings (router, synth, drums, FX, generators).
  5. CC snapshots, **spread over 10 ms chunks** so a long snapshot doesn't
     queue in front of notes for long.
* Bandwidth: a PC is 2–3 bytes; 8 devices × 16 CCs = 384 bytes ≈ 125 ms on
  DIN. Spreading keeps the backlog small; the DIN queue's realtime priority
  keeps clock bytes on time regardless.
* Panic cancels a pending recall.

## Setlist

* Up to 32 scenes in one setlist, in order. Scenes can repeat (e.g. back to
  the intro scene).
* Live view buttons: **A** previous, **B** recall the armed scene (with its
  timing), **C** next. Hold B: recall now, ignoring quantisation.
* "Armed" is the scene that B will recall: normally the next one.

## Pages

**Live view** (default):

```
┌────────────────────────────────────────┐
│Set>     USB ok  DIN 128.0 PLAY  ▮ 87%  │
│ Now   04 Main groove                   │  big, current scene
│                                        │
│ Next  05 Breakdown          next bar   │  armed scene and its timing
│       ███████████░░░░░  3 beats        │  countdown when queued
│                                        │
│ 06 Build   07 Drop   08 Outro          │  the scenes after it
│   Prev        Recall         Next      │  touch buttons A / B / C
└────────────────────────────────────────┘
```

**Scene editor** (tap the current scene name):

```
┌────────────────────────────────────────┐
│Set>     Scene 05 Breakdown             │
│ Timing  [next bar]   Tempo [ - ]       │
│ Digitakt  PC 12 ⚠   CC 3               │  ⚠ = PC switches patterns
│ Oxi       --        CC 0               │
│ OxiMate   Router ✓  Synth ✓  Drums 7   │
│           FX 2      Gen --             │
│ [Capture now]   [Learn CCs]            │
│   -            Done            +       │
└────────────────────────────────────────┘
```

* **Capture now:** stores OxiMate's current router, synth, drum pattern, FX
  preset and tempo into the scene.
* **Learn CCs:** while on, the last value of each CC arriving on the inputs
  is stored for the matching device (by channel), so you can set your gear by
  turning knobs on a controller and capture the result.
* LEDs: the countdown to a queued recall fills the bar; a recall flashes all
  LEDs white once.

## Storage

* NVS now (key `setlist`, versioned): devices, and scenes as compact message
  lists; the stored router/synth configs are referenced by small "snapshot"
  slots (up to 16 router and 16 synth snapshots), not copied into every scene.
  Estimated 6–10 KB, within NVS limits.
* SD later (with stage 13's SD support): setlists as JSON files, to keep
  several sets and back them up.

## Thread model

* The UI edits a copy of the setlist; changes are handed over like other
  configs. The MIDI task executes recalls and holds the pending-recall state.
* OxiMate settings recall goes through the same `changed()` paths as the
  pages (router, SamSynth, drum machine), so their own safety logic applies
  (note release before routing changes, SamSynth diffing).

## Files

| File | Change |
|------|--------|
| `src/scenes.*` (new) | devices, scenes, setlist, recall engine (MIDI task), command queue |
| `src/scene_page.*` (new) | live view and editor |
| `src/router.*`, `src/sam_synth.*`, `src/drum_machine.*` | small "apply this config" entry points if missing |
| `oximate.ino` | create the engine, register the page |
| `test/test_scenes.cpp`, `test/sources_scenes.mk` | host tests |

## Milestones

1. Devices and scenes with device messages only, recall now. Tests for byte
   output and the Elektron PC guard.
2. Quantised recall (bar, drum pattern end), PC lead time, CC spreading.
3. OxiMate snapshots (router, synth, drums, tempo), capture now.
4. Live view and editor pages, learn CCs, LEDs.
5. Phrase quantisation once stage 22 exists; FX and generator presets once
   stages 11 and 12 exist.
6. README, TESTING.md section, hardware test with the Elektron and the Oxi.

## Host tests

* A scene produces exactly the expected bytes, in the burst order above.
* A PC for a device without "program changes allowed" is never sent.
* Quantised recall fires on the bar boundary of a fake clock; lead-time PCs go
  out the right number of beats earlier.
* CC spreading: no more than one chunk per 10 ms.
* Panic cancels a pending recall.
* Snapshot recall goes through the router's release-before-change (a held
  note gets its note-off on the old channel).

## Risks

* **Wrong PC to an Elektron mid-set** switches its pattern: the per-device
  allow flag, the warning icon and the explicit device setup are the
  mitigation; there is no "send PC to all channels" function.
* **Two systems changing patterns** (the Elektron's own pattern chaining and
  OxiMate's PCs) can fight; document "let one of them lead".
* **NVS space** if scenes grow large; SD storage removes the limit.
* **Recall while following an external clock:** boundaries depend on the
  master's Start; a missed Start shifts them (stage 22's "bar 1 now"
  re-sync helps).

## Open questions

1. Which devices and channels would be in your scene list (e.g. Digitakt on
   its auto channel, Oxi, M8), and which of them should receive program
   changes at all?
2. Should scenes switch Elektron patterns via program change, or do you prefer
   to switch patterns on the Elektrons themselves?
3. Default recall timing: next bar, next phrase, or now?
4. How many scenes does a typical set need (proposed: up to 32)?
5. Is "Capture now" plus "Learn CCs" a good way to build scenes, or would you
   rather type values in?
