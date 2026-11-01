# Stage 22: Phrase tracker

> **Status: plan, not implemented.** Written 2026-11-01. Where this plan and the README's
> [Shared infrastructure](../../README.md#shared-infrastructure) section disagree, the README wins.
>
> The README's open questions for this stage are not answered yet. This plan uses the
> defaults in "Assumed until answered" below; each one is a single setting or constant.

Keeps track of phrasing (usually 8 or 16 bars) while playing dawless techno live, so you
always know where you are in the phrase without counting: a big counter on screen, the LED
bar filling across the phrase, a warning before the phrase ends, and (with stage 10) a
pulse you can feel.

## Goal and use cases

* **Know when the phrase turns over:** "bar 13 of 16, 4 bars left" at a glance, so fills,
  drops, mutes and pattern changes land on the phrase boundary.
* **Higher level:** "phrase 3 of 4" inside a 64-bar section (intro / build / drop).
* **Works with any clock master:** OxiMate's own clock, or following the M8, Oxi One or an
  Elektron over USB or DIN.
* **Recovers easily:** one tap on a downbeat ("Bar 1") realigns the count when OxiMate
  missed the master's Start or the count drifted from the music.
* **Optional:** named sections ("drop in 4 bars"), and opt-in actions at phrase
  boundaries (a program change, a looper start in stage 13, a scene in stage 15).

## Assumed until answered

| Setting | Default | Range |
|---------|---------|-------|
| Phrase length | 16 bars | 4, 8, 16, 32 |
| Section length | 4 phrases (64 bars) | off, 2, 4, 8 phrases |
| Warning before phrase end | 1 bar | 0 (off), 1, 2, 4 bars |
| LED display | on (while the Phrase page is open) | on / always / off |
| Vibration cues (stage 10) | phrase start + "ending soon" if stage 10 is built | per cue: off / vibrate / click / both (stage 10's `phrase` setting) |
| Realign quantise | nearest bar | — |
| Song Position Pointer | realigns the count | on / ignore |
| Program change at boundaries | off | off, or channel + program list |
| Named sections | off | off / list of up to 8 names + lengths |

## Counting model

The clock engine already counts 24 PPQN ticks from Start and knows beats per bar
(`ClockConfig::beatsPerBar`). The tracker works purely in bars:

```
bar        = tick / (24 * beatsPerBar) - offset       (offset set by realign / SPP)
barInPhrase   = bar mod phraseLength                  (0-based; shown 1-based)
phrase        = bar / phraseLength
phraseInSect  = phrase mod sectionPhrases
barsLeft      = phraseLength - barInPhrase            (counting the current bar)
```

* Everything is integer arithmetic on the bar number, so changing the phrase length or the
  section length takes effect immediately and consistently (no state to rebuild).
* `offset` is the only extra state: realign and Song Position change it, Start resets it.
* Bars are counted on the clock engine's **Bar** events (MIDI task), so the count changes
  exactly on the downbeat, not on a later UI frame.

### Where it runs

```
 MIDI task (core 0)                              UI thread (core 1)
 ClockEngine listener ─► PhraseTracker ─┬─ snapshot (CriticalSection) ─► Phrase page, LEDs, header
   Start/Stop/Locate/Bar/RawTick        ├─ CueQueue (stage 10) ─► Haptics: phrase start / ending
                                        └─ boundary actions: program change via MidiOutputs
 UI "Bar 1" / settings ─► command + config handoff (like Router / ClockEngine)
```

* **`src/phrase_tracker.*`:** pure C++ (no Arduino), host-testable. Fed clock events by a
  ClockEngine listener in the MIDI task; publishes a `PhraseSnapshot` under a
  `CriticalSection`; takes settings through the usual edit / `changed()` / pending copy.
* **Boundary actions** run in the MIDI task (they send MIDI); vibration cues go to stage
  10's `CueQueue`, scheduled ahead like stage 10's beats (`dueMicros = next downbeat -
  lead`), so the pulse lands on bar 1 rather than ~5 ms after it.

### Following a clock that was already running

Today the engine only emits Bar events while it is *running*, i.e. after a Start (FA). When
OxiMate is plugged into a master that is already playing, only clock ticks (F8) arrive and
no Start, so nothing would be counted.

* **Change in `ClockEngine`:** a `RawTick` event to listeners for every tick, running or
  not (one extra `emit()`; existing listeners ignore it).
* The tracker counts **free-running** from the first tick it sees when there was no Start:
  the count advances, but the page shows **`?`** next to the counter ("not synced") until
  you tap **Bar 1** on a downbeat, or a Start / Song Position arrives.
* With internal clock and OxiMate stopped, the counter holds (internal clock ticks
  continuously while stopped, but there is no musical position to count).

### Realign ("Bar 1")

* Tap **Bar 1** (button B on the Phrase page) on the downbeat that *should* be bar 1.
* Quantised to the **nearest bar line**: if the tap is in the first half of the current
  bar, the current bar becomes bar 1; in the second half, the *next* bar does (you tapped
  slightly early). This makes a slightly early or late tap still land right.
* Implementation: the UI sends a command with its tap time (`nowMicros64()`); the tracker
  converts it to a tick position using the engine's current tick and period, rounds to the
  nearest bar and sets `offset`. The `?` marker clears.
* **Hold B:** realign the *phrase* only (keep the section count), for when you jumped by a
  phrase on purpose.

### Song Position Pointer

* A Locate event (F2, already handled by the engine while stopped) sets the engine's tick
  to the song position; the tracker then sets `offset = 0`, treating song position as
  absolute (bar 1 of the song = bar 1 of a phrase). The `?` clears.
* Setting "SPP: ignore" keeps the manual offset instead (for masters whose song position
  doesn't match your phrasing, e.g. a pickup bar).
* Start (FA) resets the offset to 0; Continue (FB) keeps it.

## Phrase page

Title **Phrase**, after the Clock page (or after Analyse once stage 9 is merged).

```
┌────────────────────────────────────────┐
│Phrase>  USB ok  DIN 128.0 PLAY  ▮▮▮ 87%│
│                                        │
│          13 / 16                       │  big counter (Font7), 1-based
│   ████████████████████████░░░░░░       │  phrase progress bar
│   4 bars left        phrase 3 / 4      │  countdown + section level
│   Section: BUILD  ->  DROP in 4        │  (named sections only)
│ [Len 16][Sect 4][Warn 1][SPP on]       │  settings chips (tap = next value)
│ [LEDs on][PC off][Names off]           │
│   Clear        Bar 1        View       │  touch buttons A / B / C
└────────────────────────────────────────┘
```

* **Counter:** grey while stopped, white while counting, orange during the warning bars,
  white flash on bar 1. A `?` after the counter means "not synced" (see above).
* **Progress bar:** one segment per bar; the warning bars are drawn orange.
* **Buttons:** **A** Clear (reset to bar 1 at the next Start; hold A = reset now),
  **B** Bar 1 (realign; hold = phrase only), **C** View (normal / big).
* **Big view (C):** only the counter and the bar, as large as fits, for reading from a
  distance on a live table (same idea as stage 21's simple view).
* **Redraws** only when the bar changes or a setting changes (a few times per phrase plus
  the downbeat flash), drawn on `canvas()` like the Clock page's partial redraw, so the
  shared SPI bus is barely used.

Named sections view (when "Names" is on) replaces the settings rows with the arrangement
list; tapping an entry edits its length (A/C −/+), long press renames from a preset list
(INTRO, BUILD, DROP, BREAK, OUTRO, A, B, C):

```
│  # Name   Len  Start                   │
│ >1 INTRO  16   bar 1                   │  current section marked
│  2 BUILD  16   bar 17                  │
│  3 DROP   32   bar 33                  │
│  4 BREAK  16   bar 65                  │
```

## LED bar

While the Phrase page is open (or always, with "LEDs always"), the tracker claims the LED
bar (`Leds::claim`, owner 22):

| LEDs | Shows |
|------|-------|
| 0–9 | phrase progress: lit segments = `barInPhrase * 10 / phraseLength` (16 bars: one LED per 1.6 bars) |
| colour | white / dim blue normally; **orange** during the warning bars; **white flash** of all 10 on bar 1 (150 ms) |
| not synced | the lit segments blink slowly |

Panic and other warnings keep using the overlay above everything, as now. With "LEDs
always" the Clock page's own beat display wins while the Clock page is open.

## Vibration cues (stage 10)

* Stage 10's plan already reserves the cues **Phrase start** (long-long) and **Phrase
  ending soon** (triple short) and the setting `HapticConfig::phrase`.
* The tracker enqueues them into stage 10's `CueQueue` from the MIDI task: "ending soon"
  on the first warning bar's downbeat, "phrase start" on bar 1, both scheduled one bar
  period ahead minus the motor lead, like stage 10's beats.
* If stage 10 isn't built yet, the tracker compiles without it (a no-op hook), and the
  speaker click alone can be offered via `SpeakerSynth` as a fallback.

## Optional: program change at phrase boundaries (opt-in)

* Off by default. When on: a channel and a list of programs (one per phrase, or one per
  named section). The tracker sends the program change in the **last bar** of the phrase
  (at its downbeat), through `MidiOutputs` with `Origin::internal(22)`.
* **Elektron warning (shown on the page and in the README):** Elektron boxes queue a
  program change and switch at the **end of their current pattern**, not at OxiMate's
  phrase boundary. If the Elektron pattern length isn't a divisor of the phrase length (or
  its pattern was started off-phrase), the switch lands at the wrong bar. Program changes
  also go to *every* device on DIN listening on that channel (the user's Elektrons
  respond on all channels, see README decisions).
* Sending to USB is a separate switch (the M8 ignores program changes for song control).

## Interaction with other stages

* **Stage 13 looper:** "Rec at next phrase" — the looper's record start can be quantised
  to the next phrase boundary instead of the next bar, and the loop length can default to
  the phrase length. The tracker exposes `nextPhraseTick()` in its snapshot for that.
* **Stage 15 scenes:** a scene can be armed to recall at the next phrase boundary
  (same hook as the program change, but calling the scene recall instead).
* **Stage 12 generators:** "realign all slots on the next bar" (stage 12 plan) can use the
  phrase boundary as an option.
* **Header:** optional "phrase in header" setting replaces the clock text with `13/16`
  on every page (the clock is still on the Clock page).

## Settings (NVS key `phrase`, versioned)

```cpp
struct PhraseConfig {
  static constexpr uint8_t kVersion = 1;
  uint8_t version = kVersion;
  uint8_t phraseBars = 16;      // 4, 8, 16, 32
  uint8_t sectionPhrases = 4;   // 0 = off, 2, 4, 8
  uint8_t warnBars = 1;         // 0 = off, 1, 2, 4
  uint8_t leds = 1;             // 0 off, 1 page, 2 always
  bool sppRealigns = true;
  bool headerCounter = false;
  // program change at boundaries (off by default)
  uint8_t pcChannel = 0;        // 0 = off, 1-16
  bool pcToUsb = false;
  uint8_t pcPrograms[8] = {};   // per phrase in section, or per named section
  // named sections (off by default)
  uint8_t sectionCount = 0;     // 0 = off, up to 8
  uint8_t sectionName[8] = {};  // index into the preset name list
  uint8_t sectionBars[8] = {};
};
```

Saved with the usual 2 s delay, only while stopped (like the drum machine), so a flash
write never coincides with playback.

## Files

| File | Change |
|------|--------|
| `src/phrase_tracker.*` | new: counting model, realign, SPP, boundary actions, snapshot (pure C++) |
| `src/phrase_page.*` | new: Phrase page, big view, named sections, LEDs |
| `src/clock_engine.*` | add `ClockEvent::RawTick` (every tick, running or not) |
| `src/haptics.*` (stage 10) | phrase cues (already planned there) |
| `oximate.ino` | tracker global, listener registration, page after Clock/Analyse |
| `test/test_phrase.cpp`, `test/sources_phrase.mk` | host tests |

## Milestones

1. **Counting core:** `PhraseTracker` with bar / phrase / section counting from Start and
   Bar events, snapshot, host tests.
2. **Phrase page (basic):** counter, progress bar, countdown, settings chips; LEDs.
3. **Following a running clock:** `RawTick`, free-running count with the `?` marker;
   **Bar 1** realign with nearest-bar quantisation; Song Position handling.
4. **Cues:** stage 10 hooks (or the speaker-click fallback), header counter option.
5. **Named sections** and the opt-in program change at boundaries (with the warnings).
6. **Hooks for stages 13 / 15** (`nextPhraseTick()`, boundary callbacks), README and
   TESTING.md section, commit.

## Host tests

* Counting: from Start, Bar events 0…n give `barInPhrase`, `phrase`, `phraseInSection`
  and `barsLeft` for phrase lengths 4/8/16/32 and section lengths off/2/4/8;
  beatsPerBar 3/4/7.
* Changing the phrase length mid-run gives a consistent position (no jumps beyond the new
  modulo).
* Realign: taps 10 % / 40 % / 60 % / 90 % into a bar choose this bar / this bar / next bar /
  next bar; hold-B keeps the section count.
* Song Position: a Locate to 16ths `n` sets the bar to `n / (4 * beats)` and clears the
  `?`; with "SPP: ignore" the manual offset is kept; Start resets the offset; Continue
  keeps it.
* Free-running: ticks without Start count bars and set "not synced"; a tap clears it.
* Boundary actions: the program change is sent once, in the last bar, on the configured
  channel, only when enabled; the warning cue fires on the first warning bar.
* Uses the existing test helpers (`OutputRig`, `ClockLog`, `runFor`, fake time).

## Risks and open questions

* **No Start from the master:** some setups only send clock. The free-running count plus
  the **Bar 1** tap covers it, but the user has to tap once per set.
* **Song Position from the master may not match the music's phrasing** (pickup bars,
  odd-length intros): the "SPP: ignore" setting exists for that.
* **Program changes reach every listening device**, and Elektrons switch at their own
  pattern end: opt-in only, with a warning.
* **Odd meters / tempo changes:** counting is per bar using the engine's `beatsPerBar`;
  a master that changes meter mid-song isn't representable over MIDI clock anyway.
* **Open questions for the user** (from the README, still unanswered):
  1. Which box is usually the clock master in your live set, and does it send Start? Song
     Position?
  2. Phrase lengths you use: 8 and 16 only, or also 4 and 32? Do you want the higher level
     ("phrase 3 of 4")?
  3. Vibration cues: yes or no? Warning lead time: 1 bar, 2 bars, or adjustable?
  4. Named sections / arrangement list: wanted, or a plain counter?
  5. Program changes at phrase boundaries: interesting, or leave out?
  6. Should the phrase counter also appear in the header on every page?
