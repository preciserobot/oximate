# OxiMate

A pocket MIDI toolbox built on the M5Stack Core2.

The Core2 is the **brain**. Controllers plug into it: any USB MIDI device, an
Oxi One, or an Ableton Push 2. Everything it produces goes out over DIN MIDI,
to a Dirtywave M8 or other gear, and to the built-in SAM2695 General MIDI
synth.

```
 USB MIDI device ──USB──► ┌──────────────┐ ──DIN OUT──► M8 / synths
 (keyboard, Oxi One,      │ M5Stack Core2│ ──────────► SAM2695 synth (headphones)
  Push 2, M8 ...)  ◄──────│   OxiMate    │
                          └──────────────┘ ◄──DIN IN─── other MIDI gear
```

## Roadmap

| # | Stage | Implemented | Status |
|---|-------|:-----------:|--------|
| 1 | [MIDI monitor + merge](#stage-1-midi-monitor--merge) | ✅ | needs hardware test |
| 2 | [Router + LEDs](#stage-2-router--leds) | ✅ | needs hardware test |
| 3 | [Oxi One remote](#stage-3-oxi-one-remote) | — | planned (full plan) |
| 4 | [Push 2 host](#stage-4-push-2-host) | — | planned (full plan) |
| 5 | [Drum machine](#stage-5-drum-machine) | — | planned (full plan) |
| 6 | [Preview synth](#stage-6-preview-synth) | — | planned (full plan) |
| 7 | [Ear trainer](#stage-7-ear-trainer) | — | planned (full plan) |
| 8 | [Clock master](#stage-8-clock-master) | ✅ | needs hardware test |
| 9 | [Clock analyser](#stage-9-clock-analyser) | — | planned (full plan) |
| 10 | [Haptic metronome](#stage-10-haptic-metronome) | — | idea |
| 11 | [MIDI effects](#stage-11-midi-effects) | — | idea |
| 12 | [Generative sequencers](#stage-12-generative-sequencers) | — | idea |
| 13 | [Recorder / looper](#stage-13-recorder--looper) | — | idea |
| 14 | [SysEx librarian](#stage-14-sysex-librarian) | — | idea |
| 15 | [Setlist / scenes](#stage-15-setlist--scenes) | — | idea |
| 16 | [Panic & diagnostics](#stage-16-panic--diagnostics) | ✅ | needs hardware test |
| 17 | [BLE MIDI](#stage-17-ble-midi) | — | idea |
| 18 | [Network MIDI](#stage-18-network-midi) | — | idea |
| 19 | [Extra controls](#stage-19-extra-controls) | — | idea |
| 20 | [DFAM MIDI-to-CV](#stage-20-dfam-midi-to-cv) | — | idea |

Details for each stage are in [Stages](#stages).

## Hardware

* **M5Stack Core2**: ESP32, 320×240 touch screen, touch buttons A/B/C, battery,
  speaker, vibration motor, IMU, microphone, SD card, Wi-Fi and BLE.
* **M5Stack USB Host Module v1.2** (MAX3421E): USB host port for MIDI devices.
* **M5Stack GO Bottom2**: Grove ports B and C, 10 RGB LEDs, battery.
* **M5Stack Unit MIDI (U187)**: DIN/TRS MIDI IN and OUT, plus a SAM2695
  General MIDI synth with a headphone output. Connect it to **Port C** (blue)
  of the GO Bottom2.
* A USB MIDI class-compliant device.

### USB Host Module: pin mapping

The Core2's SPI pins differ from the defaults in USB Host Shield 2.0. Set the
module's DIP switches as follows:

SPI  | GPIO | Comment
-----|------|--------
MOSI | 23   |
MISO | 38   |
SCK  | 18   |
SS   | 19   | Set this on the USB board.
INT  | 35   | Set this on the USB board.

> Do not use 33 for `SS`. The Core2 uses it for the I2C bus (IMU, touch, etc.).
> The display shares the SPI bus with the USB module.

### Unit MIDI: wiring and mode switch

The following comes from the Unit MIDI schematic. M5Stack's prose
documentation is ambiguous on this point.

* Grove → Core2 Port C: the Core2 receives on **G13** and transmits on **G14**,
  at 31250 baud (M5Unified's `port_c_rxd`/`port_c_txd`).
* **DIN IN always reaches the Core2**, in both switch positions.
* **The Core2's output always drives the SAM2695 synth.**
* The front switch only selects the source for **DIN OUT**:
  * **Separate / "from core"**: DIN OUT carries the Core2's output. **Use this
    setting for OxiMate.** DIN OUT then carries the merged stream.
  * **Bypass**: DIN OUT is a hardware thru of DIN IN. The Core2's output only
    reaches the SAM2695, so USB MIDI does not reach DIN OUT.
* **If the SAM2695 goes silent:** corrupted or non-MIDI data (e.g. an Oxi One
  set up for an Oxi Split) can leave it muted. It keeps that state until it
  loses power, and the Core2's reset button doesn't cut its power. Power
  everything off fully (unplug USB, hold the power button for 6 s), then test
  with the Diag page's test note (hold B). Stage 6 will re-send the synth's
  settings at boot to make this less likely.

### About the Core2's own USB-C port

The Core2's USB-C port is a USB-serial bridge (CH9102), not a USB MIDI
device. It can't appear as a MIDI port on a computer. All USB MIDI goes through
the USB Host Module.

## Controls

* **Switch page:** **swipe left or right** across the screen, at least half
  its width (left = next page, right = previous), or tap the header bar at the top. The page
  name with `>` on the left of the header shows where you are: Monitor →
  Router → Clock → Diag → Monitor.
* **Touch buttons A/B/C** (the three circles below the screen) belong to the
  current page; the bottom bar shows their labels. Holding a button for about
  0.5 s is a separate action on some pages.
* **Power button** (left side): short press = **panic** (note-offs, all notes
  off, Stop; the header shows PANIC and the LEDs flash red); hold about 1.5 s
  = **panic all** (also a note-off for every
  note on every channel, about 2 s on DIN). Holding it for 6 s still powers
  the Core2 off.
* **Reset button** (underneath): restarts the device.

| Page | A | B | C |
|------|---|---|---|
| Monitor | Clear the log | Pause / resume | Show / hide clock messages |
| Router | All channels on | LED brightness | Default settings |
| Clock | Start / Stop (hold while stopped: Continue) | Tap tempo | Outputs view |
| Diag | Reset counters | Next view (hold: test note, middle C on channel 1 to DIN OUT and the synth) | Panic (hold: panic all) |

## Architecture

These rules apply to every stage.

* **Pages and modes.** The UI is a set of pages, such as Monitor, Router and,
  later, Drum machine and Push 2. MIDI merging and routing keep running in the
  background whichever page is shown. Later stages add pages, and a mode menu
  once there are more than a few.
* **Buttons.**
  * **Swipe left/right** anywhere, or **tap the header bar**, to switch page.
    The 20-pixel header alone turned out to be too small a target.
  * **Touch buttons A/B/C always belong to the current page.** Each page shows
    their labels in the footer.
  * Navigation never takes a button away from a stage that needs it, such as
    the drum machine or clock master.
* **Settings** are saved in NVS (the ESP32's flash key-value store) and
  restored at boot. Saves are delayed by 2 s so a burst of taps causes one
  flash write.
* **Memory.** The code and core logic of all stages fit easily: stage 1 uses
  8% of flash. Large buffers go in PSRAM (8 MB): full-screen sprites, the
  Push 2 frame, the looper. Recordings and SysEx files go to the SD card.
* **Timing.** The screen, the USB host module and the SD card share one SPI
  bus. Clock-accurate stages (8, 12, 20) will move MIDI and clock handling to
  the second CPU core so screen redraws can't delay them.
* **BLE and Wi-Fi** (stages 17 and 18) each fit, but running both at once uses
  most of the internal RAM. Expect them to be separate modes.

### LED bar (M5GO Bottom2)

There are 10 SK6812 RGB LEDs on GPIO25, as two strips of 5, one along each
side of the base. M5Unified drives them through the ESP32's RMT peripheral
(hardware-timed, so updates don't affect MIDI timing). OxiMate configures all
10; M5Unified only sets up 1 on the Core2. Updates run at about 60 fps or
less.

* **Default (input activity).** LEDs 0–4 flash for USB input, LEDs 5–9 for
  DIN input. Verify which physical side is which.
  * Note-on: colour by pitch class (C = red, around the colour wheel to B),
    brightness by velocity.
  * Other channel messages: dim white. SysEx: magenta.
  * Each flash fades out in about 100 ms.
* **Brightness** is a setting: off, low, medium or high. At full brightness
  the 10 LEDs draw up to about 0.5 A, so the medium level is capped at about
  1/8 of full.
* **Later modes take the LEDs over:**
  * Clock master: beat pulse, with a different colour on the downbeat; bar
    position on one side and phrase position on the other.
  * Clock analyser: green when stable, yellow when jittery, red when the clock
    drops out.
  * Sequencers, drum machine and DFAM: the playhead (8 steps map one-to-one
    onto the DFAM).
  * Euclidean sequencer: hits around the "ring" of 10 LEDs.
  * Per-channel activity for channels 1–10.
  * VU or CC bar meters, a colour per mode, battery level, a connection
    "breathing" light, a pulsing red light while recording, and warnings
    (stuck note, SysEx overflow).

## Shared infrastructure

The stage plans were written separately. These shared pieces apply to all of
them. Where a stage plan says something different, this section wins. Each
piece is built by the first stage that needs it.

| Piece | Owner | What it is |
|-------|-------|------------|
| MIDI task on core 0 | 8 | 1 ms polling of USB/DIN, router, clock engine; UI stays on core 1; ring buffers between them; SPI bus mutex |
| `MidiOutputs` | 16 | the **only** send path, used by the router and every generator; feeds the note tracker and TX listeners; DIN realtime priority (8); `sendRaw` |
| `NoteTracker` | 16 | held/sustained notes per output and channel as sent; panic and auto-release |
| `Origin` | 16 | who caused a message: `UsbIn`, `DinIn` or `Internal(owner stage)`. This replaces the `MidiSource::Internal` + `Router::sendInternal()` proposals in stages 3, 4 and 5 |
| `MidiMessage::timeMicros` | 9 | timestamp taken at the input |
| Input interceptors | 3, 4 | modules that **consume** a device's input before routing (Oxi remote, Push 2) |
| Input observers | 7, 9 | pages and modules that **watch** input after routing (trainer, analyser) |
| LED claims | all | a mode claims a range of LEDs (`claim(owner, first, count)`, `setPixel`, `clear`, `release`). Unclaimed LEDs show input activity. Stage 16 warnings overlay everything |
| `ClockEngine` | 8 | the only source of outgoing clock and transport |
| `SamSynth` | 6 | the only code that talks to the SAM2695; stages claim parts |
| `StepSequencer` | 4 | pattern/step core shared by the Push 2 sequencer and the drum machine |

### Decisions (2026-11-01)

| Topic | Decision |
|-------|----------|
| Oxi One | **MK1** (remote protocol model byte `00`); firmware version still to check |
| M8 | **Model:02**, connected to OxiMate's MIDI OUT with **TRS type A**. Check which TRS type the Unit MIDI's 3.5 mm jacks use; if it isn't type A, use the DIN jack with a DIN-to-TRS-A adapter. The M8's control channel is still to be read off the M8 |
| Push 2 | the power supply is available. Tracks are **switchable** between drum and melodic, use **channels 1, 2, 3 …** in order, and go to **DIN OUT** (the Push takes the USB port) |
| SD card | available, and will be fitted when a stage needs it (drum banks, patterns, recordings) |
| Channel 16 | no gear uses it. Reserved for SAM2695 settings |
| Elektron gear | it responds on all channels, so in Separate mode it receives synth-setup program changes (channel 16) and trainer notes (channel 15). **Program changes switch Elektron patterns.** Either turn off program-change receive (or the relevant channels) in the Elektron MIDI settings, or put the Unit MIDI in **Bypass** while using stages 6/7. Stage 6 shows a warning about this |
| Clock output | sent to **USB by default**, switchable to DIN (per output) |
| External clock lost | OxiMate **stops** |
| Panic trigger | **power button** (short press = levels 1+2, ~1.5 s hold = level 3) |
| Panic and transport | panic also sends **Stop** (`FC`) |
| Stuck notes | **released automatically** after the threshold (default 30 s) |
| Drum machine | **32 steps** (two pages of 16 pads). A USB pad controller can **play lanes live and record** into steps |

**Channel 16 is reserved** for SAM2695 settings (stage 6). The ear trainer
defaults to channel 15. The Oxi encoder CCs use the M8's control channel.

**Built so far (2026-11-01):** the MIDI task on core 0 (`src/midi_task.*`)
with the event ring to the UI, the SPI bus lock (`src/spi_bus.*`), DIN
output with realtime priority, timestamps on every message, `MidiOutputs`
and `NoteTracker`, and the router sending through them with its settings
handed over safely between cores. The `ClockEngine` with its
`esp_timer` wake-up and LED claims came with stage 8; the panic job, LED
overlay and the MIDI task loop hook with stage 16. Not built yet: input
interceptors/observers as a general mechanism (stage 16 observes input
directly in `onMidi()`).

**Suggested build order:** first the stage 8 timing core (its milestones 1–2)
and the stage 16 output layer (its milestone 1). These are the foundation.
After that: 6 → 5 and 7 → 3 → 4 → 9 and the rest of 8 and 16. The
hardware checks are independent and can be done now with the stage 2
firmware: the Oxi protocol check (stage 3, M0) and the Push 2 full-speed
test (stage 4, M0).

## Stages

### Stage 1: MIDI monitor + merge

#### MIDI routing

Every input goes to every output, except back to the port it came from:

| From \ To   | DIN OUT + SAM2695 | USB device |
|-------------|:-----------------:|:----------:|
| USB device  | ✔                 | ✘ (no echo)|
| DIN IN      | ✔ (software thru) | ✔          |

* These are the defaults. Stage 2 makes every route configurable on the
  Router page.
* Both inputs are parsed into **complete messages** before they're forwarded,
  so the merged stream never interleaves two messages. SysEx is assembled in
  full (up to 1024 bytes; longer messages are dropped) and sent in one piece.
  Realtime bytes (clock, start/stop) pass through immediately.
* DIN input handles running status. Output never uses running status.
* USB cable numbers are kept, so DIN-to-USB messages go to cable 0.
* DIN carries about 3000 bytes per second. A USB device can send faster than
  that, so a 1 KB transmit buffer absorbs bursts. Sustained overload will slow
  the main loop down.

#### Display

```
┌────────────────────────────────────────┐
│Monitor> USB 1C75:0288   DIN 120.0 PLAY │  header: USB device VID:PID, clock
│USB   1 NoteOn   C4   100               │
│USB   1 NoteOff  C4   0                 │  log: newest at the bottom
│DIN  10 CC       74   127               │  USB lines cyan, DIN lines orange
│USB2  1 Bend     +512                   │  USB2 = USB cable 2
│DIN  -- SysEx 6: 7E 7F 06 01            │  grey = dropped by router
│  ...                                   │
│   Clear        Pause       Clock:off   │  touch buttons A / B / C
└────────────────────────────────────────┘
```

* Note names use MIDI note 60 = C4.
* Clock and Active Sensing messages are hidden from the log by default.
  Instead, the header shows the BPM and transport state of whichever source
  most recently sent clock.
* **A**: clear the log. **B**: pause or resume the log; forwarding continues
  while paused. **C**: show or hide Clock and Active Sensing in the log.
* Every logged line is also printed on the serial port at 115200 baud.

### Stage 2: Router + LEDs

Stage 2 was originally "send USB MIDI to the attached MIDI port". Stage 1
already does that, so stage 2 makes the routing configurable instead. The LED
bar behaviour is described under Architecture.

#### Router page

```
┌────────────────────────────────────────┐
│Router>  USB 1C75:0288   DIN 120.0 PLAY │  tap the header: next page
│ [USB>DIN]   [DIN>DIN]    [DIN>USB]     │  routes (tap to toggle)
│ USB in channels                        │
│ [1][2][3][4][5][6][7][8]               │  tap a channel to pass or block it
│ [9][10]...         [16]                │
│ DIN in channels                        │
│ [1]...                                 │
│ [Clk INT][SysEx on][USB>keep][DIN>keep] │
│   All ch       LEDs:med     Defaults   │  touch buttons A / B / C
└────────────────────────────────────────┘
```

* **Routes**: turn USB→DIN, DIN→DIN (software thru) and DIN→USB on or off
  individually.
* **Channel filter** per input: blocked channels are dropped before routing.
* **Clock**: since stage 8 this chip is a shortcut for the Clock page's
  source (`Clk INT` / `Clk USB` / `Clk DIN`). Clock and transport never pass
  through the router; the clock engine is their only sender.
* **SysEx**: forward it or not.
* **Force channel** per input (`USB>keep` / `USB>ch5`): rewrites the channel
  of every channel message from that input. Each tap moves to the next value:
  keep → 1 → … → 16 → keep.
* Buttons: **A** turns all channels back on, **B** cycles the LED brightness,
  **C** restores the default settings.
* The Monitor shows every input message. Lines for messages the router drops
  are shown in grey.
* **SAM2695 caveat:** the synth chip and DIN OUT are wired to the same Core2
  output (see Unit MIDI wiring), so anything sent to the synth also reaches
  DIN OUT. Setting the SAM2695 voice is therefore part of stage 6, which
  covers this.

### Stage 3: Oxi One remote

Turn the Oxi One into a control surface for the M8: OxiMate puts the Oxi
into remote mode, drives its grid LEDs, button LEDs and screen, and
translates its pads, buttons and encoders into M8 MIDI.

* **Protocol is public:** OXI's Remote Control Reference spreadsheet (MK1 and
  MKII). Messages start with `F0 00 21 5B <model> 01`; values are sent as
  nibbles; Oxi input comes back as plain notes/CCs on channels 1–2.
  Milestone 0 checks it against your firmware before anything else.
* **M8 side:** buttons and mute/solo as notes on the M8's control channel
  (community-documented; the M8 has no official chart), encoders as CCs you
  MIDI-learn in the M8, and a playable keyboard plus mute/solo rows on the grid.
* **Connection:** the Oxi takes the USB host port, so the M8 connects over
  TRS/DIN. The M8 doesn't report mutes back; a Sync button re-sends state.
* New files: `oxi_protocol.h`, `oxi_remote.*`, `oxi_m8.*`, `oxi_page.*`.

**Full plan:** [docs/stages/stage-3.md](docs/stages/stage-3.md)

### Stage 4: Push 2 host

Use an Ableton Push 2 as a step sequencer and control surface, with OxiMate
as its host.

* **Go/no-go first:** the USB host module only does full speed; whether the
  Push 2 works at full speed is undocumented. Milestone 0 tests it with the
  stage 2 firmware and the Push's own power supply.
* Uses the Push's **Live port** in its default mode; the USB MIDI driver
  already ignores the display interface. Full MIDI map, LED palette and
  animation SysEx are in the plan.
* **Sequencer:** 8 tracks (drum or melodic), 64 steps, note/velocity/length/
  probability per step, 16 patterns, driven by stage 8's clock engine. Its
  core, `src/step_sequencer.*`, is reused by stage 5.
* **Play mode:** keyboard pads, encoders as CCs, touch strip.
* Tracks switch between drum and melodic, use channels 1, 2, 3 … and go
  to DIN OUT.
* LED updates are batched and only sent on change. The Push display stays
  dark (one frame takes 0.5–1 s at full speed); an optional experiment only.
* DIN→USB is switched off while a Push is connected.

**Full plan:** [docs/stages/stage-4.md](docs/stages/stage-4.md)

### Stage 5: Drum machine

A touch-screen drum machine playing the SAM2695 drum kits (and DIN OUT) on
channel 10.

* 8 lanes × **32 steps** (two pages of 16 pads); each step off, on or accent; 16 patterns per bank;
  queued pattern switches and chaining; swing from the clock engine.
* **UI:** one lane at a time as 16 large pads with an overview of all lanes
  below (an 8×16 grid of tappable cells doesn't fit a 2" screen). Grid,
  Sound and Pattern views. Buttons: A Play/Stop, B Mute, C View.
* **LED bar:** left strip = beat 1–4, right strip = 16th within the beat.
* Note-offs half a step after each hit, registered with the note tracker.
* Built on stage 4's `step_sequencer`; bank in NVS, banks as JSON on SD.
* Drums always go to both SAM2695 and DIN OUT in Separate mode (wiring);
  Bypass mode is the documented "synth only" option.

**Full plan:** [docs/stages/stage-5.md](docs/stages/stage-5.md)

### Stage 6: Preview synth

Control the SAM2695 synth and preview any input on it, without disturbing
external gear on DIN OUT.

* **Solves the shared-wire problem:** a GS SysEx (from the datasheet)
  assigns each synth part to a channel or mutes it. The synth then plays
  only the channels you pick ("listen map"). Instrument, volume and effect
  settings go out on a reserved **settings channel 16**, which external gear
  should not use.
* **`src/sam_synth.*`:** stages claim synth parts instead of hard-coding
  channels, then set program, volume and effects and audition notes. State is
  re-sent at boot and after any reset that reaches DIN OUT.
* **Synth page:** 4 melodic layers plus a drum layer; GM browser by family,
  the MT-32 bank and drum kits. A/C = −/+, B = Browse/Test.
* The bundled M5-SAM2695 library is not used: two of its SysEx messages are
  wrong and its `reset()` sends System Reset to all gear. Bytes go out raw.
* Settings in NVS under their own key (`synth`).

**Full plan:** [docs/stages/stage-6.md](docs/stages/stage-6.md)

### Stage 7: Ear trainer

Two pages for ear training on the SAM2695.

* **Ear trainer:** intervals, chord qualities, scale degrees and melody
  echo; four levels plus auto-level; weak spots come up more often. Answer
  by tapping or by playing on a USB/DIN keyboard (any octave counts).
* **Chord explorer:** pick root, type and inversion to hear and see a chord;
  chords played on a keyboard are named.
* Notes play on a claimed synth part, default **channel 15**. Gear listening
  on that channel (or in omni mode) also plays them; use Bypass mode then.
* A small local note scheduler handles playback (questions don't follow
  transport or swing). LEDs flash green/red and show the streak.
* Theory and trainer logic are plain C++ and unit-tested on the Mac.

**Full plan:** [docs/stages/stage-7.md](docs/stages/stage-7.md)

### Stage 8: Clock master

Internal MIDI clock, and the **timing architecture every later stage uses**.

* **MIDI task on core 0:** high priority, woken by a hardware timer, polls
  USB and DIN every 1 ms and runs router and clock engine. The UI stays in
  `loop()` on core 1. Lock-free ring buffers carry events to the UI and
  config snapshots back.
* **Shared SPI bus:** one mutex; the MIDI task only try-locks; the Monitor
  pushes its screen in 4 bands so the bus is never held more than ~3 ms.
* **DIN output:** realtime bytes get priority, so clock is delayed at most
  ~1.6 ms.
* **`ClockEngine`:** internal or follow USB/DIN (lock/lost detection), tap
  tempo (touch or IMU tap), swing on step events only (MIDI clock stays
  straight), per-output dividers/multipliers, sub-tick scheduler, listener
  events (tick, step, beat, bar, transport).
* **Single clock owner:** the engine becomes the only sender of clock and
  transport; `RouterConfig` v2 migrates the old clock setting;
  `ClockTracker` is retired.
* **Clock page:** big BPM, source chips, outputs view. A Start/Stop, B Tap,
  C Outputs.

**Implemented (2026-11-01)**, with these differences from the plan:

* The timer is a one-shot `esp_timer` (same microsecond time base as
  `micros()`) instead of a raw hardware timer; its callback only wakes the MIDI
  task.
* Tap tempo is computed on the UI side and written to the tempo setting, so
  nothing writes settings back across cores. A tap doesn't realign the beat
  phase yet.
* Internal clock is sent continuously, also while stopped, so followers
  always know the tempo. Step, beat and bar events only run while playing.
* Tempo is changed with ±1 / ±0.1 chips; dragging the digits isn't built.
  "Rewind" sends song position 0 while stopped.
* The clock takes over the LED bar only while the Clock page is visible.
* Not built yet: the metronome events for stage 10 and the gate output for
  stage 20.

**Full plan:** [docs/stages/stage-8.md](docs/stages/stage-8.md)

### Stage 9: Clock analyser

Measure the quality of incoming MIDI clock on USB and DIN.

* Tick interval min/avg/max, jitter (std dev and histogram), BPM now and
  averaged, drift, dropouts, transport and song-position checks,
  double-clock detection, USB vs DIN phase.
* **Timestamps must improve first:** today they're taken after `loop()`
  polls, so a screen redraw adds up to ~15 ms of error (a tick at 120 BPM
  is 20.8 ms). Fix: `MidiMessage::timeMicros`, DIN bytes stamped on arrival
  (~320 µs constant offset, < 50 µs spread), USB stamped per transfer in
  stage 8's 1 kHz task (±1 ms).
* Four views: Stats, Histogram, Drift, Phase. LEDs green < 0.5 ms jitter,
  yellow above, red on dropout. CSV over serial.

**Full plan:** [docs/stages/stage-9.md](docs/stages/stage-9.md)

### Stage 10: Haptic metronome

The vibration motor pulses on the beat, together with the LED bar, for
practice without headphones.

### Stage 11: MIDI effects

Arpeggiator, chord generator, scale quantiser, transpose, velocity curves and
CC scaling. They're applied in the merge path between input and output, so
they combine with the router's settings.

### Stage 12: Generative sequencers

Euclidean rhythms, with the hits shown around the "ring" of 10 LEDs. A
looping random sequence that slowly mutates.

### Stage 13: Recorder / looper

Record the merged stream into PSRAM, then save it to the SD card as `.mid`
files and play it back. The LEDs pulse red while recording.

### Stage 14: SysEx librarian

Send `.syx` files from the SD card and capture SysEx dumps from devices. The
Subharmonicon files in `subH_sysex_user/` are the first test case.

### Stage 15: Setlist / scenes

Send program changes and CC snapshots to several devices with one touch.

### Stage 16: Panic & diagnostics

Get out of stuck-note situations quickly, and see what's going on.

* **`MidiOutputs` + `NoteTracker`:** one send path for the router and every
  generator; the tracker records held and sustained notes per output and
  channel *as sent* (after remapping), with who caused them. ~3.2 KB.
* **Panic:** level 1 = note-offs for tracked notes; level 2 adds CC123,
  CC120 and CC64=0 on all channels; level 3 adds all 128 notes × 16
  channels (~1.3 s on DIN, non-blocking).
* **Trigger:** power button. Short press = levels 1+2, ~1.5 s hold =
  level 3 (a 6 s hold still powers off). Fallbacks: header hold, button C on
  the Diagnostics page.
* **Automatic release:** on USB disconnect, Active Sensing timeout, and
  before a router change takes effect. Stuck notes are released automatically after 30 s.
* **Diagnostics page:** channel activity, message counters and rates, DIN
  TX buffer, dropped SysEx, USB errors, heap/PSRAM, battery.

**Implemented (2026-11-01)**, with these differences from the plan:

* Level 3 sends ordinary 3-byte note-offs (about 2 s on DIN) instead of a
  running-status stream (1.3 s), so other output never has to pause.
* Stuck notes are released after 30 s, as decided; there is no setting for
  the threshold yet.
* The header-hold fallback isn't built; C on the Diag page does the same.
* Counters: per input and message type, filtered, output totals, DIN and USB
  queue drops, SPI-bus-busy skips, UI events dropped, UI loop time. Not yet:
  DIN receive overruns and USB error codes.
* The Diag page shows channels 1–10 on the LED bar while it's open; panic
  progress overlays the LED bar on any page.

**Full plan:** [docs/stages/stage-16.md](docs/stages/stage-16.md)

### Stage 17: BLE MIDI

Wireless MIDI to and from an iPad or Mac. The ESP32 supports BLE MIDI
natively, and the `BLE-MIDI`/NimBLE libraries are already installed.

### Stage 18: Network MIDI

RTP-MIDI / AppleMIDI over Wi-Fi. This probably can't run at the same time as
BLE MIDI (see Architecture).

### Stage 19: Extra controls

Add M5 Grove units (8-Encoder, Dual Button, Angle) as physical knobs and
buttons alongside the touch buttons.

### Stage 20: DFAM MIDI-to-CV

Feasibility notes.

The DFAM has no MIDI, but its patch bay has CV and gate inputs: Trigger, VCA
CV, Velocity, VCA Decay, External Audio, VCF Decay, Noise Level, VCO Decay,
VCF Mod, VCO 1 CV, 1→2 FM Amount, VCO 2 CV, Tempo, Run/Stop and Adv/Clock.

**Playing a specific step (1–8).** The DFAM has no "go to step N" input.
OxiMate can keep track of the current step and, on a MIDI note, send a fast
burst of pulses to **ADV/CLOCK** to reach the target step. For example, notes
C1–G1 select steps 1–8. Things to test on real hardware:

* Do the skipped steps sound? A burst well under a millisecond may only be
  heard as the last step.
* How to get back in sync after the front-panel ADVANCE button is pressed:
  a "set step 1" button on OxiMate, or a reset via Run/Stop if that resets
  the sequence.

**Modulation.** MIDI CCs drive CV inputs such as VCF Mod, Decays, Noise Level,
VCA CV and Velocity. Note velocity can drive the Velocity input.

**Hardware.** The Core2 alone is not enough:

* The ESP32's built-in DACs are 8-bit and 0–3.3 V. GPIO25 drives the LED bar,
  so only GPIO26 (Port B) is free. That is good enough for one rough
  modulation CV, but not for pitch.
* GPIO gates are 3.3 V. Moog's semi-modular trigger inputs expect about
  +3.2 V or more (the Mother-32 figure), which leaves almost no margin. Buffer
  gates to 5 V.
* Recommended: a small CV board on I2C (Port A), for example an **MCP4728**
  (4 × 12-bit DAC) with op-amp scaling to the DFAM's ±5 V range. 12 bits gives
  2.4 mV steps over ±5 V, about 1/30 of a semitone, which is fine for 1 V/oct
  pitch once it's calibrated. Add 2–3
  transistor gate buffers (ADV/CLOCK, Run/Stop, Trigger) driven from free
  GPIOs. An off-the-shelf alternative is the M5Stack **DAC 2 Unit** (GP8413,
  2 × 15-bit, 0–5/0–10 V on I2C). It gives unipolar CVs only, so you'd still
  need buffers for the gates.
* Before wiring anything, check the exact input ranges in the DFAM manual.
  A Moog forum user reports that some gate inputs only accept 0–5 V.

**Plan:** prototype the step jump with one buffered gate on ADV/CLOCK first.
If skipping is inaudible and sync is reliable, build the MCP4728 CV board.

Sources:
[DFAM spec listing](https://reverb.com/ca/p/moog-dfam-drummer-from-another-mother-analog-percussion-synthesizer),
[Moog forum: DFAM voltages](https://forum.moogmusic.com/viewtopic.php?p=179088),
[Moog forum: Run/Stop and ADV/CLK](https://forum.moogmusic.com/viewtopic.php?p=186955),
[Mother-32 manual: clock input](https://www.manualslib.com/manual/988819/Moog-Mother-32.html?page=47).

## Build and upload

Requires `arduino-cli` with the `esp32:esp32` core, plus the **M5Unified** and
**M5GFX** libraries.

```sh
make build                               # fetches + patches USB Host Shield 2.0 on first run
make send PORT=/dev/cu.usbserial-XXXX    # upload
make                                     # build + send
make log SECONDS=20                      # reset the board and show its serial output
```

### Unit tests

```sh
make test     # host build of the parsers, router, note tracker, output queues and event ring
```

The tests compile the hardware-independent modules on the Mac against the
stand-ins in `test/stubs/`.

### USB Host Shield 2.0 (patched)

On the first build, the `Makefile` clones USB Host Shield 2.0 at commit
`ef5b046` into `libraries/`. It then applies `USB_Host_Shield_2.0.patch`,
which sets the Core2 pin mapping above, and marks the version as
`1.7.0-core2patch`.

### Code layout

| File | Purpose |
|------|---------|
| `oximate.ino`       | setup and loop: starts the MIDI task, then drains its events into the UI |
| `src/midi_task.*`   | MIDI task on core 0 (polls USB and DIN every 1 ms) and the event ring to the UI |
| `src/midi_outputs.*`| the single send path (DIN, USB), with counters |
| `src/note_tracker.*`| held and sustained notes per output, as sent |
| `src/spi_bus.*`     | lock for the SPI bus shared by display, USB host and SD |
| `src/sync.h`        | small critical sections between the two cores |
| `src/midi_message.h`| `MidiMessage`, the common type for complete messages |
| `src/usb_midi.*`    | USB host: USB-MIDI packets ⇄ messages, including SysEx assembly |
| `src/din_midi.*`    | DIN serial: byte stream (running status, realtime) ⇄ messages |
| `src/monitor.*`     | the Monitor page |
| `src/router.*`      | filtering, remapping, routing; settings saved in NVS |
| `src/router_page.*` | the Router page |
| `src/leds.*`        | GO Bottom2 LED bar |
| `src/clock_engine.*` | internal/followed clock, transport, swing, output ratios, scheduler |
| `src/clock_page.*`  | the Clock page (tempo, source, swing, outputs, LED beat display, IMU tap) |
| `src/tap_tempo.h`   | tap tempo averaging |
| `src/platform_time.h` | 64-bit microsecond clock (esp_timer) |
| `src/ui.*`          | page switching, header and footer |
| `src/panic.*`       | panic levels 1–3, automatic release (USB unplug, Active Sensing, stuck notes) |
| `src/diagnostics.h` | input statistics |
| `src/diag_page.*`   | the Diag page |

## Credits

Based on [MIDIDump](https://github.com/gdsports/MIDIDump) by gdsports, which
is MIT licensed.
