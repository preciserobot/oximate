# Stage 10: Haptic metronome

> **Status: plan, not implemented.** Written 2026-11-01. Where this plan and the README's
> [Shared infrastructure](../../README.md#shared-infrastructure) section disagree, the README wins.

Cues you can feel (vibration motor) or hear (a click on the built-in
speaker), without looking at the screen: a metronome, and opt-in alerts for
things that matter live.

## Goal and use cases

* **Metronome:** a pulse on every beat, a stronger one on the downbeat,
  following OxiMate's clock (internal or followed). Practice without
  headphones; keep time when the room is loud.
* **Count-in:** one bar of pulses before Start takes effect.
* **Alerts (each opt-in):**

| Cue | Source | When |
|-----|--------|------|
| Clock lost / locked | ClockEngine (MIDI task) | following an external clock |
| Phrase start / phrase ending | stage 22 (later) | bar 1 of a phrase; N bars before its end |
| Touch tick | Ui (UI thread) | a touch button or chip is pressed |
| Panic done | Panic summary (UI thread) | a panic finished |
| Feel the kick | input observer (MIDI task) | chosen notes, e.g. 36 on channel 10 |
| Ear trainer right / wrong | stage 7 page (UI thread) | an answer is judged |
| Low battery | header battery read (UI thread) | below 15%, once per 5 minutes |

## The motor (what the hardware allows)

* **API:** `M5.Power.setVibration(level)` (M5Unified). On the Core2 it sets
  the power chip's LDO3 to `480 + 12 × level` mV (AXP192; DLDO1 on the
  AXP2101 of newer Core2s), level 0 = off. From M5Unified's
  `Power_Class.cpp`, checked in the installed library.
  * LDO3 spans 1.8–3.3 V, so useful levels are about **110 (1.8 V, weak)
    to 235 (3.3 V, full)**; below ~110 the request is below the regulator's
    range.
  * Each call is one write on the **internal I2C bus** (touch, IMU, power
    chip): well under 1 ms, but it must stay on the UI thread, because
    M5Unified's `I2C_Class` has no locking of its own (checked: no mutex in
    `I2C_Class.*`).
* **Motor type:** an eccentric-rotating-mass (ERM) motor. ERM motors of this
  size typically need **~20–50 ms to spin up** and a similar time to stop
  ([Precision Microdrives AB-004, ERM characteristics](https://www.precisionmicrodrives.com/ab-004)).
  The Core2's exact motor isn't documented; milestone 1 measures it.
* **Consequences for patterns:**
  * pulses shorter than ~40 ms are barely felt; 60–120 ms is clear
  * two pulses need a gap of ≥ 80 ms to feel separate
  * strength steps beyond about 3 levels aren't distinguishable; use
    **length and rhythm** to tell cues apart, strength only as a secondary
    hint
  * a fast metronome (> ~200 BPM, beats < 300 ms apart) still works with
    80 ms pulses

## Patterns

Each cue is a short sequence of `(level, milliseconds)` segments; `0` = off.

| Cue | Pattern | Feel |
|-----|---------|------|
| Beat | 235 × 70 | short tap |
| Downbeat (bar) | 235 × 140 | long tap |
| Count-in beat | 235 × 70 (last one 140) | taps leading in |
| Phrase start | 235 × 140, 0 × 90, 235 × 140 | long-long |
| Phrase ending soon | 235 × 60, 0 × 80, 235 × 60, 0 × 80, 235 × 60 | triple short |
| Clock lost | 235 × 400 | one long buzz |
| Clock locked | 180 × 60, 0 × 80, 235 × 60 | quick double |
| Touch tick | 200 × 25 | barely there (on purpose) |
| Panic done | 235 × 250 | long |
| Feel the kick | 235 × (velocity-scaled 40–90) | follows the kick |
| Right answer | 235 × 60 | tap |
| Wrong answer | 235 × 60, 0 × 80, 235 × 60, 0 × 80, 235 × 60 | triple |
| Low battery | 160 × 200, 0 × 200, 160 × 200 | soft double |

**Priority** when cues overlap (a higher one interrupts a lower one, a lower
one is dropped while a higher one plays): clock lost / panic > phrase >
downbeat > beat / count-in > kick > answer > battery > touch tick.

**Spin-up lead:** for cues whose time is known in advance (beats while the
clock is internal or locked), the pulse starts `leadMs` early (default
25 ms, set from the measurement in milestone 1) so the felt peak lands on
the beat.

## Speaker click (alternative or extra)

* The same cues as short clicks on the built-in speaker via `SpeakerSynth`
  (one of its 8 tone channels reserved for clicks): beat 1 kHz × 15 ms,
  downbeat 2 kHz × 25 ms, alerts as two-tone blips.
* Output setting per cue group: **Vibrate**, **Click**, **Both**, **Off**.
* No spin-up, so no lead; latency is the speaker's buffer (~10–20 ms, see
  stage 6).
* The speaker and the microphone share I2S: a future tuner would turn the
  click off while it runs.

## Architecture

```
 MIDI task (core 0)                          UI thread (core 1, loop())
 ClockEngine listener ─┐                     ┌─ Haptics::service() ── M5.Power.setVibration
 (Beat, Bar, Start,    ├─► CueQueue (SPSC) ─►│        └─ SpeakerSynth click
  Stop, lost/locked)   │   {cue, dueMicros}  │
 kick observer ────────┘                     └─ UI sources call Haptics::cue() directly
                                                (touch, panic, trainer, battery)
```

* **`src/haptics.*` (UI thread):** a non-blocking pattern player.
  `cue(Cue, uint32_t dueMicros = 0)` starts a pattern now or at `dueMicros`;
  `service()` advances it, calling `setVibration` only when the level
  changes. It also triggers the speaker click.
* **`CueQueue`:** a small SPSC ring (32 entries) like `EventRing`, written by
  a ClockEngine listener and an input observer in the MIDI task, drained by
  `Haptics::service()`.
* **Advance scheduling:** the ClockEngine listener knows the period, so for
  beat N it enqueues beat N + 1 with `dueMicros = now + period − leadMs`.
  The UI fires it on time, independent of when the beat event itself
  arrives. If the tempo changes, the next real beat event corrects the
  schedule.
* **Service points, so UI stalls don't delay cues:** `loop()` calls
  `service()` every iteration, and `Ui::pushContent()` calls it between its
  8 slices. A full redraw takes ~70 ms under load, but the longest gap
  between service calls is then one slice plus its yield, **~5 ms**.
* **Expected timing:** beat events reach the UI within ~1–5 ms; with advance
  scheduling the felt pulse lands within roughly ±5 ms of the beat plus the
  motor's own variation. Unscheduled cues (kick, touch) add the spin-up time.

## Settings (NVS key `haptic`, versioned)

```cpp
enum class CueOutput : uint8_t { Off, Vibrate, Click, Both };

struct HapticConfig {
  static constexpr uint8_t kVersion = 1;
  uint8_t version = kVersion;
  CueOutput metronome = CueOutput::Off;   // beat + downbeat
  bool metronomeOnlyWhenPlaying = true;
  bool countIn = false;                   // one bar before Start (internal clock)
  CueOutput clockAlerts = CueOutput::Off; // lost / locked
  CueOutput phrase = CueOutput::Off;      // stage 22
  bool touchTick = false;
  bool panicDone = true;
  bool lowBattery = true;
  bool trainer = true;                    // stage 7 answers
  bool kick = false;                      // feel the kick
  uint8_t kickChannel = 10, kickNote = 36;
  uint8_t strength = 235;                 // full; patterns scale from it
  uint8_t leadMs = 25;                    // spin-up compensation
};
```

## Where the settings live (recommendation)

A third view on the **Clock page**, "Cues", rather than a new page:
* the metronome and count-in belong with tempo and transport;
* the page count is already 8, and swiping past one more page costs time
  live;
* C on the Clock page already cycles views (Main → Outputs → **Cues**).

```
┌────────────────────────────────────────┐
│Clock>   USB ok  INT 120.0 PLAY   ▮87%  │
│ Metronome  [Vibrate] [Only playing]    │  tap to cycle Off/Vib/Click/Both
│ Count-in   [ off ]   Lead [ 25 ms ]    │
│ Clock lost/locked      [ Both ]        │
│ Phrase cues (st. 22)   [ Off  ]        │
│ Kick  [on] ch[10] note[36 C1]          │  tap a value, A/C -/+
│ Touch tick [off]  Panic [on]  Batt [on]│
│ Trainer [on]      Strength [====··]    │
│                                        │
│   Start          Tap          Main     │  touch buttons A / B / C
└────────────────────────────────────────┘
```

* A and B keep their Clock-page meaning (Start/Stop, Tap) so the metronome
  can be checked right there; values are changed by tapping chips, and
  tapping a selected value chip steps it (−/+ zones on its left/right
  halves).
* **Hold B** on the Cues view: play each cue pattern once in priority order
  (a "feel test").

## LEDs

No new claims: the Clock page already shows the beat on the LED bar. While
a clock-lost cue plays, the panic-style overlay is reused briefly (red) on
every page, since that alert matters more than the current page's LEDs.

## Files

| File | Change |
|------|--------|
| `src/haptics.*` | new: pattern player, priorities, scheduled cues, speaker click |
| `src/haptic_patterns.h` | new: the pattern table (pure data, host-testable) |
| `src/cue_queue.h` | new: SPSC ring MIDI task → UI (or reuse `SendQueue`'s pattern) |
| `src/clock_engine.*` | none: a listener is registered from `oximate.ino` |
| `src/clock_page.*` | Cues view |
| `src/ui.*` | `service` hook between content slices; touch tick hook |
| `src/ear_page.*` | replace the direct `setVibration` call with `Haptics::cue()` |
| `oximate.ino` | listener + kick observer registration, `haptics.service()` in `loop()`, battery and panic cues |

## Milestones

1. **Measure the motor:** a throwaway test that pulses at levels 110–255
   and lengths 20–200 ms while timing with the IMU's accelerometer (sample
   at 1 kHz, find vibration onset after `setVibration`). Gives spin-up time,
   felt minimum length and the useful level range; sets `leadMs`.
2. `Haptics` pattern player + priorities + host tests (fake clock and a
   fake `setVibration` recording level changes).
3. Metronome from ClockEngine with advance scheduling; service points in
   `Ui::pushContent()`.
4. Speaker click output.
5. Alerts: clock lost/locked, panic, battery, touch tick, kick, trainer.
6. Cues view on the Clock page, NVS settings, README and TESTING.md.

## Host tests

* Pattern timing: segments start and stop at the right times; level changes
  only when needed.
* Priority: a downbeat interrupts a beat; a touch tick is dropped while a
  beat plays; clock lost interrupts everything.
* Scheduling: a cue with `dueMicros` fires at that time (± the service
  interval), not when enqueued; tempo change re-aligns.
* Count-in: exactly `beatsPerBar` pulses, the last one long, then Start.
* CueQueue overflow drops and counts.

## Risks and open questions

* **Motor variation:** spin-up and felt strength vary by unit and battery
  voltage; milestone 1 measures, `leadMs` and strength stay adjustable.
* **Audible buzz** near a microphone; the click output and "Only playing"
  help.
* **Battery:** at 120 BPM with 70 ms pulses the motor runs ~14% of the time
  (roughly 50–80 mA while on, typical for ERM motors); a long set on
  battery will notice. Show the motor's share on the Diag page?
* **I2C contention:** all vibration writes stay on the UI thread; if a later
  stage wants cues from another task, it must go through `CueQueue`.
* Open questions for you:
  1. Settings as a "Cues" view on the Clock page (recommended), or a separate
     page?
  2. Metronome default: vibrate, click, both, or off?
  3. Should the count-in also send a MIDI count-in (e.g. clicks on a channel)
     to DIN, or stay local?
  4. Feel-the-kick: just one note (default 36 on channel 10), or a small list
     (kick + snare)?
