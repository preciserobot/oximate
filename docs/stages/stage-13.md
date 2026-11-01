# Stage 13: Recorder / looper

> **Status: plan, not implemented.** Written 2026-11-01. Where this plan and the README's
> [Shared infrastructure](../../README.md#shared-infrastructure) section disagree, the README wins.

Records MIDI as it passes through OxiMate and plays it back. There are two
ways to use it:

* **Looper (recommended for live sets):** record N bars in sync with the
  clock, play them back in a loop, overdub further passes, undo the last
  pass. It is built for dawless techno: catch a riff from the Oxi One or a
  keyboard and loop it while you move on to the next part.
* **Recorder:** record everything that passes through (a whole set if you
  like), with timestamps, and save it to the SD card as a Standard MIDI File
  (`.mid`) for a DAW later. It is a "black box" that is always running.

Both share the same capture and storage code. The looper is the first
milestone; the recorder and SD come after it.

## What is captured

| Source | Captured | Why |
|--------|----------|-----|
| Input messages (USB, DIN) | yes, via an `InputHooks` observer in the MIDI task, with `timeMicros` | what you played |
| Messages the router dropped | no (looper), optional (recorder, "raw" setting) | the loop should sound like what you heard |
| OxiMate's own generators (drums, ear trainer) | no (looper), optional (recorder) | they can re-create themselves |
| Clock and transport (F8, FA, FB, FC, F2) | never stored as events | timing comes from the clock engine; the SMF gets a tempo map instead |
| SysEx | recorder only, up to 256 bytes each | rarely useful in a loop, but nice in a set recording |
| Active Sensing (FE) | never | noise |

**Looper input filter** (setting): `USB`, `DIN` or both, and one channel or
all (default: all, except channel 10 when the drum machine runs). The
recorder takes everything.

## Event storage (PSRAM)

One packed record per event, in a PSRAM buffer allocated at boot:

```cpp
struct LoopEvent              // 8 bytes
{
  uint32_t tick;              // position in 1/96 quarter notes (4x MIDI clock) since loop/recording start
  uint8_t  status, data1, data2;
  uint8_t  flags;             // source (USB/DIN), pass number (overdub layer, 0-15)
};
```

* **Resolution:** 96 PPQN, stored as ticks rather than microseconds. Loops
  then follow tempo changes and line up with the clock engine. At 120 BPM
  one tick is 5.2 ms, which is coarser than the ~1 ms input timestamps but
  finer than DIN can deliver a chord (three notes take ~3 ms). The position
  between two 24-PPQN clock ticks is interpolated from `timeMicros` and the
  engine's measured tick period.
* **Recorder:** the same records in a separate ring buffer, plus a 32-bit
  microsecond time so free (unclocked) recording works too. SysEx goes into
  a side buffer and the record holds its offset.
* **Capacity:** the looper buffer is 64 K events (512 KB). That's far more
  than any loop needs (a dense 16-bar loop is a few thousand events). The
  recorder ring is 2 MB (256 K events, about an hour of busy playing).
  Together that is about 2.5 MB of the ~4 MB PSRAM; the off-screen canvas
  uses 150 KB.
* **Threading:** events are appended in the MIDI task only. The UI reads a
  snapshot (count, length, pass) under a `CriticalSection`. The SD writer
  task reads the recorder ring through its own tail index (single producer,
  single consumer).

## Looper behaviour

```
 Rec ──► (waits for the next bar) ──► recording N bars ──► plays in a loop
  ▲                                                          │
  │            Overdub: records a new pass on top ◄──────────┤
  │            Undo: removes the last pass                   │
  └──────────────── Clear ◄──────────────────────────────────┘
```

* **Length:** 1, 2, 4, 8 or 16 bars (setting), or "free": press Rec again to
  end, rounded to whole bars.
* **Start:** recording starts on the next bar line (the clock engine's `Bar`
  event), so a loop always starts on beat 1. Notes played up to one
  sixteenth early count as the downbeat.
* **Clock needed:** the looper only runs while the clock engine runs
  (internal or followed). With the clock stopped, Rec arms and waits for
  Start.
* **Playback:** a `ClockEngine` listener in the MIDI task. On each 24-PPQN
  tick it schedules the events of the next tick window with
  `ClockEngine::schedule()` at their exact sub-tick time, and sends them via
  `MidiOutputs` with `Origin::internal(13)` to the same outputs the router
  would use (setting: DIN, USB or both).
* **Overdub:** while playing, Rec records a new pass on top (pass number
  +1). Notes held across the loop end are cut at the end and restarted at
  the start, so the loop never accumulates hanging notes.
* **Undo:** removes all events of the last pass. Redo is not planned.
* **Hanging notes:** stop, clear, undo and leaving the page call
  `NoteTracker::release(..., &Origin::internal(13))`. A recorded note-on
  without a note-off (you were still holding it when the loop closed) gets a
  note-off inserted at the loop end.
* **Quantise** (setting: off, 1/16, 1/8, 1/16T): applied when a pass ends,
  to note-ons only; the note's length is kept. "Off" is the default, so the
  feel stays human.
* **Mute while overdubbing** (setting): the input that is being recorded
  isn't also played back from earlier passes on the same channel. This
  avoids doubled notes when replacing a part. Off by default.
* **Several loops?** One loop with up to 16 passes in this stage. Several
  independent loops (one per channel, launched separately) are a natural
  next step and fit the same storage.

## Recorder behaviour

* Off, armed (records from the next Start), or always on (records
  everything since boot, ring buffer).
* **Save** writes the buffer to the SD card as `/oximate/rec/YYYYMMDD-HHMMSS.mid`,
  with the time from the Core2's real-time clock (or a counter if it isn't
  set).
* Without clock, events are stored with microsecond times and written at a
  fixed 120 BPM, 960 PPQN, so a DAW still shows them in real time.

## Standard MIDI Files

`src/smf.*`, plain C++ with no Arduino dependency (host-tested):

* **Writing:** type 1 when the recording has more than one source or
  channel (track 0 = tempo map, then one track per channel), type 0
  otherwise. 96 PPQN for clocked material (the native resolution), 960 for
  free recordings.
* **Tempo map:** the recorder stores a tempo event whenever the clock
  engine's tempo changes by more than 0.1 BPM, and a time-signature event
  from its beats per bar.
* **Reading** (for loading loops): types 0 and 1, any PPQN (rescaled to
  96), running status, meta events skipped except tempo and time signature,
  SysEx kept. Files over the looper's 64 K events are refused with a
  message.
* **Writing streams:** the SMF track length is only known at the end, so
  the writer leaves a placeholder and seeks back to patch it.

## SD card on the shared SPI bus

The Core2's SD slot (chip select GPIO 4) shares the SPI bus with the
display and the USB host module.

* **Library:** the ESP32 Arduino core's own `SD` library. It uses Arduino
  `SPIClass` with transactions, the same as the USB Host Shield library, so
  both work under our bus lock. SdFat is faster but would add a third SPI
  driver style; not needed for MIDI-sized files.
* **Never from the MIDI task.** A low-priority **SD task** on core 1 does all
  card access: mount, directory listing, save, load. It takes the bus with
  `SpiLock("sd")` around each operation chunk:
  * writes in chunks of at most 4 KB (about 3–5 ms of bus time each), so USB
    polling keeps the ~5 ms gap the stage 16 load test established;
  * FAT updates and file close can take longer (directory and FAT sector
    writes, sometimes 10–50 ms on slow cards). They happen only at the end of
    a save; the README will say "don't save during a gig-critical moment".
* **Mount** at boot (or when a card appears) at 20 MHz; if it fails, the
  page shows "No SD card" and the recorder only keeps its PSRAM buffer.
* **Without an SD card** the looper works fully (it never needs the card);
  saving and loading are greyed out.

## Looper page

```
┌────────────────────────────────────────┐
│Loop>    USB ok   INT 120.0 PLAY  ▮ 87% │
│                                        │
│   ● REC   bar 3 / 8     pass 2         │  state, position, pass
│ ▕████████████░░░░░░░░░░░░░░░░░░░░▏     │  loop progress
│ ch1 ▪▪ ▪ ▪▪   ▪▪ ▪ ▪▪   ▪▪ ▪ ▪▪        │  piano roll strip:
│ ch2   ▪   ▪     ▪   ▪     ▪   ▪        │  one row per channel
│ ch10▪ ▪ ▪ ▪ ▪ ▪ ▪ ▪ ▪ ▪ ▪ ▪ ▪ ▪        │
│ [8 bars][Q off][In all][Out DIN]       │  setting chips
│ [Undo] [Clear] [Save] [Load]           │
│    Rec         Play/Stop     Overdub   │  touch buttons A / B / C
└────────────────────────────────────────┘
```

* **A Rec:** arm / record a new loop (on an empty looper) or end a free
  recording. **B:** play/stop the loop. **C:** overdub on/off. Holding A
  undoes the last pass.
* **Recorder view** (tap the title chip): state, recording length, events,
  buffer use, and Save.
* **File browser** (Load / Save): a list of `.mid` files in `/oximate/`,
  newest first, 8 per screen. Tap to select; C loads, A goes back.

## LED bar

| State | LEDs |
|-------|------|
| Armed | red, slow pulse |
| Recording | red, fast pulse; loop progress fills the bar |
| Playing | green progress across the 10 LEDs over the loop |
| Overdubbing | orange progress |
| Saving | blue sweep |

Panic and warnings overlay this, as everywhere. Claimed only while the page
is visible, except "recording" (pulsing red on every page while a loop or
recording is armed or running, so you don't forget it).

## Settings (NVS)

Key `looper`, versioned: loop length, quantise, input and channel filter,
outputs, mute-while-overdub, recorder mode. The loop content itself is not
kept across power cycles unless saved to SD.

## Files

| File | Purpose |
|------|---------|
| `src/loop_buffer.*` | PSRAM event storage, passes, undo, quantise (host-tested) |
| `src/looper.*` | MIDI-task side: capture observer, clock listener, scheduling, note safety |
| `src/recorder.*` | ring buffer recording, tempo map |
| `src/smf.*` | Standard MIDI File writer and reader (host-tested) |
| `src/sd_store.*` | SD task: mount, list, chunked save and load under `SpiLock` |
| `src/looper_page.*` | the Loop page, recorder view and file browser |
| `oximate.ino` | globals, observer and clock listener registration, page |

## Milestones

1. **Loop buffer + looper core** (no SD): record N bars on the next bar line,
   loop playback via `schedule()`, note safety, host tests for timing and
   wrap-around.
2. **Overdub, undo, quantise**, mute-while-overdub.
3. **Loop page** and LEDs.
4. **SMF writer/reader** with host round-trip tests.
5. **SD task**: mount, chunked save, file browser, load into the looper.
   Measure bus holds with the existing `bus held` statistics.
6. **Recorder** (ring buffer, tempo map, Save). README, TESTING.md section,
   commit.

## Host tests

* **Timing:** synthetic clock + notes → events land on the expected ticks;
  sub-tick interpolation from `timeMicros`; a note one sixteenth early counts
  as the downbeat.
* **Loop playback:** two cycles of a 1-bar loop produce the same messages at
  the same scheduled times; tempo change between cycles scales them.
* **Note safety:** held across the loop end → cut and restarted; stop →
  every note released; undo removes exactly one pass.
* **Quantise:** note-ons move to the grid, lengths are kept.
* **SMF:** write → read round trip (type 0 and 1, running status, tempo and
  time signature, SysEx); a reference file from a DAW parses correctly.

## Risks

* **SD on the shared bus:** FAT updates can hold the bus for tens of
  milliseconds, which stretches the USB poll gap during a save. Saves are
  chunked and happen on demand only; if it's still noticeable, saving can be
  limited to while the clock is stopped (like the drum machine's NVS save).
* **Card compatibility:** some cards don't like SPI mode at 20 MHz; fall back
  to 10 MHz on mount failure. FAT32 only (exFAT cards need reformatting).
* **PSRAM speed:** appending 8-byte records is cheap, but scanning a large
  loop for each tick window must stay bounded: the buffer is kept sorted by
  tick, and playback keeps a read cursor instead of scanning.
* **Echo/loop interaction:** looped output that comes back on DIN IN (a
  device with MIDI thru) must not be recorded again. The stage 16 echo
  protection drops it before the router; the looper's observer skips
  messages that weren't forwarded.

## Open questions

1. Looper first, recorder second: is that the right priority?
2. Loop lengths: 1, 2, 4, 8, 16 bars enough, or also 32 and free length?
3. Should playback go to DIN only, or also to USB (e.g. the M8)?
4. One loop with overdub passes, or several independent loops (per
   channel / per instrument) from the start?
5. Quantise default: off (human feel) or 1/16?
6. Is an SD card available to keep in the Core2 permanently?
