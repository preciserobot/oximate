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
| 3 | [Oxi One remote](#stage-3-oxi-one-remote) | — | deferred (M8 not available; needs Oxi protocol check over USB) |
| 4 | [Push 2 host](#stage-4-push-2-host) | — | planned (full plan) |
| 5 | [Drum machine](#stage-5-drum-machine) | ✅ | needs hardware test |
| 6 | [Preview synth](#stage-6-preview-synth) | ✅ | needs hardware test |
| 7 | [Ear trainer](#stage-7-ear-trainer) | ✅ | needs hardware test |
| 8 | [Clock master](#stage-8-clock-master) | ✅ | needs hardware test |
| 9 | [Clock analyser](#stage-9-clock-analyser) | ✅ | needs hardware test |
| 10 | [Haptic metronome](#stage-10-haptic-metronome) | ✅ | needs hardware test |
| 11 | [MIDI effects](#stage-11-midi-effects) | ✅ | needs hardware test |
| 12 | [Generative sequencers](#stage-12-generative-sequencers) | ✅ | needs hardware test |
| 13 | [Recorder / looper](#stage-13-recorder--looper) | ✅ | looper done (recorder to do); needs hardware test |
| 14 | [SysEx librarian](#stage-14-sysex-librarian) | ✅ | needs hardware test |
| 15 | [Setlist / scenes](#stage-15-setlist--scenes) | ✅ | needs hardware test |
| 16 | [Panic & diagnostics](#stage-16-panic--diagnostics) | ✅ | needs hardware test |
| 17 | [BLE MIDI](#stage-17-ble-midi) | ✅ | needs hardware test |
| 18 | [Network MIDI](#stage-18-network-midi) | ✅ | needs hardware test |
| 19 | [Extra controls](#stage-19-extra-controls) | — | planned (full plan) |
| 20 | [DFAM MIDI-to-CV](#stage-20-dfam-midi-to-cv) | — | planned (full plan; needs the CV board) |
| 21 | [Chord detector](#stage-21-chord-detector) | ✅ | needs hardware test |
| 22 | [Phrase tracker](#stage-22-phrase-tracker) | ✅ | needs hardware test |

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
  Router → FX → Synth → Drums → Gen → Clock → Analyse → Phrase → Set → Loop → SysEx → Ear → Chord → Radio → Diag → Monitor.
* **Header bar** (always visible): page name, `USB ok` / `USB --`, clock
  source, tempo and transport, a cyan `SPK` while the built-in speaker is the
  output, and on the right the battery level (green
  above 50%, yellow from 20%, red below 20%, cyan with `+` while charging).
  The USB device's VID:PID is on the Diag page's system view.
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
| FX | − selected setting (hold: repeat) | Bypass the shown input's chain (hold: save both chains to the shown preset) | + selected setting (hold: repeat) |
| Synth | − selected value (hold: repeat) | Browse (instrument cell) / Test (layer row); **hold: output chip ↔ built-in speaker** | + selected value (hold: repeat) |
| Drums | Play / Stop (following an external clock: Arm / Disarm) | Mute lane (hold: solo); Sound view: audition | Next view (Grid → Sound → Pattern) |
| Gen (list) | Previous slot (hold: realign all slots on the next bar) | Mute (Turing: lock / unlock; hold: reseed; Euclid hold: new rotation) | Next slot |
| Gen (edit) | − selected setting (hold: repeat) | as in the list | + selected setting (hold: repeat) |
| Clock | Start / Stop (hold while stopped: Continue); with a count-in set, Start counts in first | Tap tempo, from the 4th tap; the big BPM digits are a tap target too (Cues view: Test, plays every cue) | Next view: Main → Outputs → Cues (hold: **cues on / off**, any view) |
| Analyse | Reset statistics | Next view (Stats, Histogram, Drift, Phase) | Graph input USB/DIN (hold: CSV streaming on/off) |
| Loop | Rec: arm (empty), disarm, cancel the first pass (hold: undo the last pass) | Playback on / off | Overdub on / off |
| Set (live) | Previous scene | Recall the armed scene with its timing (hold: recall now) | Next scene |
| Set (editor / devices) | − selected value | Done | + selected value |
| SysEx (library) | Selection up | Load the file (then B sends it, A goes back) | Selection down |
| SysEx (send) | Back (while sending: cancel after the current message) | Send | |
| SysEx (capture) | Start / stop capturing | Save to SD (hold: send the capture back) | Discard |
| Phrase | Reset (the current bar becomes bar 1) | Bar 1: tap on a downbeat, rounded to the nearest bar (hold: restart the phrase, keep the section count) | Normal / big view |
| Ear (trainer) | Replay | Next (Skip before answering) | Level 1 → 4 → Auto |
| Ear (explorer) | Play | Next inversion | Block / Arpeggio |
| Chord | Clear history and held notes (hold: freeze / unfreeze) | Full / simple (big chord) view | Sharps / flats |
| Radio | Reboot into the chosen radio mode (shown only when it differs from the running one; boots the other firmware for the other radio) | BLE with central on: Devices (scan; tap a device to connect and remember it; then B scans again, C goes back) | Forget the remembered BLE device / the saved Wi-Fi network |
| Diag | Reset counters | Next view (hold: test note, middle C on channel 1 to DIN OUT and the synth) | Panic (hold: panic all) |

### What runs when

Pages are only views: swiping changes what you see and what A/B/C do. All MIDI
work runs in the background, whichever page is visible.

* **Always running:** merging and routing, the clock engine, panic and
  automatic release, synth chip control, chord detection, clock analysis and
  phrase counting. The last three only measure.
* **Plays only when started:** the drum machine (A Play on Drums, while the
  clock runs), Gen slots (once unmuted, while the clock runs), the ear trainer
  (only while its page is open), effects (chains you switch on), and the
  looper (when recording or playing).
* Drums, Gen and live input all play together in sync. LEDs and haptic cues
  belong to the visible page (cues are off by default).

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

* **Default (input activity).** LEDs 0–4 (the **right** side of the base,
  seen from the front) flash for USB input, LEDs 5–9 (the **left** side) for
  DIN input. Checked on the hardware.
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
| `Cues` (`src/cue_queue.h`) | 10 | haptic/click cue list and a queue any task can post to; stage 10's player consumes it (stage 22 posts phrase cues) |
| `SamSynth` | 6 | the only code that talks to the SAM2695; stages claim parts |
| `StepSequencer` | 4 | pattern/step core shared by the Push 2 sequencer and the drum machine |
| `music_theory` | 7 / 21 | note names, interval/chord/scale tables, pitch-class sets, exact chord matching with inversions. **Created by stage 7**; stage 21 extends the chord table (appending after `Add9`) and adds the scoring matcher both use |
| `SamSynth::claimPart()` | 6 / 7 | parts 5–14 for other stages: `claimPart(channel)`, `setPart()`, `releasePart()`; set up via the config channel, re-applied after resets (added by stage 7) |
| `ChordInput` | 21 | input observer in the MIDI task: the exact set of notes held at the inputs, published as a snapshot |
| `kInputs` / `kOutputs` | 17 | four inputs (`MidiSource` USB, DIN, BLE, NET) and four outputs (`MidiOutput` DIN, USB, BLE, NET); every per-input/per-output array uses these. `isWired()`: only USB and DIN have filters, effects, clock following and chord input |
| Route matrix | 17 | `RouterConfig` v3: `routes[input]` is a bit mask of outputs (`routed()`, `setRouted()`), plus channel mask and force channel per input; v1/v2 settings migrate. Never back to the same device (USB→USB, BLE→BLE, NET→NET) |
| `RadioPort` / `RadioTransport` | 17 | a radio output for `MidiOutputs` (`setPort()`), and the transport interface (begin in setup, `poll()` in the MIDI task, status/scan on the UI side). `RadioConfig` in NVS (key `radio`); mode chosen at boot. No clock over radio (`radioCarries()`) |
| `PacketRing` | 17 | single-producer single-consumer ring of variable-length packets between a radio stack's task and the MIDI task |

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
| BLE / network MIDI (stages 17, 18) | one radio at a time (switching reboots); radio input routed by default; **no clock over radio**; BLE: iPad + Mac as peripheral **and central mode** (BLE controllers, possibly the Oxi One); network: Mac or iPad, OxiMate's **own network** by default (joining an existing one optional), Wi-Fi power save off while network MIDI is on |
| External clock lost | OxiMate **stops** |
| Panic trigger | **power button** (short press = levels 1+2, ~1.5 s hold = level 3) |
| Panic and transport | panic also sends **Stop** (`FC`) |
| Stuck notes | **not** released on a timer: long pads are held on purpose. Automatic release only on USB unplug, Active Sensing loss and routing changes |
| Drum machine | **32 steps** (two pages of 16 pads). A USB pad controller can **play lanes live and record** into steps |

**Channel 16 is reserved** for SAM2695 settings (stage 6). The ear trainer
defaults to channel 15. The Oxi encoder CCs use the M8's control channel.

**Built so far (2026-11-01):** the MIDI task on core 0 (`src/midi_task.*`)
with the event ring to the UI, the SPI bus lock (`src/spi_bus.*`), DIN
output with realtime priority, timestamps on every message, `MidiOutputs`
and `NoteTracker`, and the router sending through them with its settings
handed over safely between cores. The `ClockEngine` with its
`esp_timer` wake-up and LED claims came with stage 8; the panic job, LED
overlay and the MIDI task loop hook with stage 16. Input hooks
(`src/input_observers.h`: interceptors that may consume a message before
routing, observers after routing, both in the MIDI task),
`Page::onMidi()` for the visible page on the UI side, and
`MidiTask::send()` (a queue for pages to send MIDI from the UI thread) were
added before stages 5, 7 and 9.

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
* **Loop protection:** a device connected both ways (DIN OUT → device →
  DIN IN) that echoes its input (MIDI thru) would otherwise make the software
  thru send the same messages round forever, flooding DIN and the SAM2695.
  A channel message arriving on DIN IN that matches one sent to DIN OUT in the
  last 50 ms is treated as an echo and not forwarded (counted on the Diag
  page as "loop echoes").
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

**Implemented (2026-11-01)**, with these differences from the plan:

* **No separate `step_sequencer` module yet:** the pattern store and playback
  live in `src/drum_machine.*`. Stage 4 can extract a shared core when it
  needs one.
* **32 steps** (two pages of 16 pads, "Pg 1/2" chip), as decided. Patterns are
  1–32 steps long.
* **Tapping a pad cycles off → on → accent → off** instead of hold-for-accent,
  which the plan flagged as awkward on a 2" screen.
* **Pad controller:** an `InputHooks` interceptor in the MIDI task. Notes on
  the **pad channel** (default 10, set in the Sound view, "off" disables it)
  whose note matches a lane's note play that lane straight away and are
  consumed, so they aren't routed a second time. Other notes pass through
  unchanged. With **Rec** on (Grid view) and the clock running, hits are
  written into the playing pattern, quantised to the nearest step;
  velocity ≥ 110 records an accent.
* **One output channel** for all lanes (10); per-lane channels, the kit
  program change, the choke pair, and SD banks (milestone M5) aren't built.
  Choose the SAM2695 drum kit on the Synth page's drum layer.
* **Saving:** the bank (8 lanes, 16 patterns, chain) is saved in NVS 2 s
  after the last edit, but only while stopped. A flash write stalls both CPU
  cores for a moment, which would disturb playback timing.
* **Play/Stop:** with the internal clock, A starts and stops the clock engine.
  When following an external clock, transport belongs to the master, so A
  arms or disarms the drum machine instead.
* **Built-in speaker:** when the Synth page's output is "Speaker", drum hits
  are also played on the Core2's speaker (the chip is silent in that mode).
* **LED bar** while the page is open: right strip = 16th within the beat plus
  a hit flash; left strip = beat within the bar plus a marker for steps
  17–32.

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
* **Output choice (decided 2026-11-01):** "Preview sound: synth chip / built-in
  speaker". The SAM2695's audio only reaches the Unit MIDI's headphone jack
  (the Grove cable carries no audio), so speaker mode uses a small software
  synth on the Core2 played through its own speaker (M5Unified `Speaker`):
  * a few voices of simple waveforms, or short samples from the SD card
  * about 10–20 ms latency; small mono speaker, fine for previews, the ear
    trainer (stage 7), metronome clicks (stage 10) and a tuning tone
  * the speaker and the microphone can't be used at the same time
  * in speaker mode nothing is sent to DIN OUT for the preview, which also
    avoids the shared-wire problem with external gear

**Implemented so far (2026-11-01):**

* `src/sam_synth.*`: listen modes All / Layers / Off, 4 melodic layers + a
  drum layer, configured through the config channel; master volume, reverb
  type and level, chorus level, voices (Full / Lite / Dry); test chords and
  browser previews; re-sends everything after a reset passes through to DIN
  OUT, and at boot when the settings differ from the chip's defaults (in All
  mode with default effects nothing is sent at boot).
* Synth page (between Router and Clock): tap a cell, A/C −/+ (hold to
  repeat), B = Browse on an instrument cell, Test on a layer row. Browser:
  16 GM families, GM / MT-32 bank, the 5 drum kits; tapping a sound previews
  it, C selects it, A goes back.
* Shows a warning in Separate wiring that settings reach DIN gear.
* Message encodings come from the datasheet; there are golden-byte tests in
  `make test`.
* **Output: chip or built-in speaker**: one global setting on the Synth page
  (bottom-row chip, or hold B on the Synth page). The header shows `SPK` while
  the speaker is the output. In speaker
  mode the chip is silenced and the Core2 plays the layer channels (All
  mode: every channel, channel 10 as drums) and the test and preview sounds
  through its own speaker, so previews send nothing to DIN. The drum machine,
  the ear trainer and generated notes (Gen slots, later the looper) also play
  on the speaker. Generated notes reach it through `src/local_sound.h`, a
  `MidiOutputs` listener. It has 8 voices,
  one soft wave for every instrument, and simple blips for drums; the volume
  follows the master volume.
* **Not built yet:** per-layer pan and chorus send, MT-32 instrument names, following program changes from
  your gear on the page, and the Synth page LEDs.
* Still to check on the hardware (TESTING.md section 6): a part keeps its
  program after being reassigned, the CM-64 kit number, and the voices
  setting.

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

**Implemented (2026-11-01)**, with these differences from the plan:

* **One page, two modes** ("Ear"): the trainer and the chord explorer
  switch with a chip (`Explorer>` / `<Trainer`) instead of being two pages,
  so there is one page less to swipe through.
* Questions play on SAM2695 part 14 (it already defaults to channel 15),
  claimed with the new `SamSynth::claimPart()` and set to acoustic grand
  piano through the config channel while the page is open. With the Synth
  page's output on "Speaker" they play on the built-in speaker and nothing
  goes to DIN.
* Keyboard answers come from any input and channel **except 10** (drums)
  and **15** (the trainer's own notes, which a device with MIDI thru would
  echo back).
* Notes are sent with `MidiTask::send()` from a small scheduler on the UI
  side; leaving the page sends note-offs for every note still sounding.
* Levels: C cycles 1 → 2 → 3 → 4 → Auto. Auto moves up after 10 answers at
  90% or better and down below 50%, without asking.
* Statistics (per exercise, streak, best streak, weak-spot weights, levels,
  vibration on/off) are saved in NVS under `trainer`.
* LEDs: green or red flash on an answer, then the streak in blue; in the
  explorer, the chord tones in their pitch-class colours. A short vibration
  on each answer (switchable).
* Not built: answer input restricted to one device or channel, solfège,
  absolute-pitch exercises. The Monitor still has its own note-name
  function (moving it to `music_theory` was left out to keep the merge
  small).

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
  phase yet. Since 2026-11-02: tempo only from the 4th tap (B shows "Tap 2/4"),
  average of the last 4 intervals, a gap of ~2 intervals counts as a missed
  tap and is filled in, other taps more than 30% off are ignored, a pause over
  3 s starts again; tapping the big BPM digits counts as a tap.
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

**Implemented (2026-11-01)**, with these differences from the plan:

* Statistics run in the MIDI task: an `InputHooks` observer feeds clock and
  transport messages (with their `timeMicros`) into `ClockAnalyser`, one
  `ClockStats` per input plus `ClockPhase`. The page reads copies taken
  under a short critical section. All of it runs in the background, so the
  numbers are ready when the page is opened.
* **Timestamp resolution is about 1 ms, and the page shows it** ("Res ~1 ms").
  DIN bytes are stamped when the MIDI task reads the UART, every 1 ms. USB is
  stamped per transfer (1 ms frames, read every 1 ms; ticks sharing a
  transfer are counted as "batched" and left out). The per-byte DIN
  `onReceive` timestamps (plan M2) aren't built: with the 1 ms MIDI task
  they would improve DIN from ±1 ms to ~0.3 ms. That's worth doing only if
  sub-millisecond DIN jitter matters. A quantisation of 1 ms adds about
  0.3 ms to the measured jitter (standard deviation), so "STABLE" (< 0.5 ms)
  is still reachable.
* Double-clock detection looks for alternating short/long intervals in the
  rolling window. A Song Position while running is flagged as `SPP!`.
* Page buttons: A reset, B view, C the input shown in the graphs (USB/DIN).
  Holding C toggles CSV streaming (`input,tick,time_us,interval_us,batched`,
  one line per tick); the Monitor's serial echo and the 2 s status line pause
  meanwhile. Lines that don't fit the serial buffer are dropped, never
  waited for.
* The whole page redraws 4 times per second (graphs included), to keep the
  shared SPI bus free.
* LEDs while the page is open: right side = USB, left side = DIN, coloured by
  status and pulsing on every beat.

**Full plan:** [docs/stages/stage-9.md](docs/stages/stage-9.md)

### Stage 10: Haptic metronome

The vibration motor pulses on the beat, together with the LED bar, for
practice without headphones.

The same motor can give other cues that work without looking at the
screen. Each is opt-in:

* phrase start and "phrase ending soon" (stage 22)
* a count-in before Start
* clock **lost**, or locked again after being lost, when following an
  external clock
* a short tick when a touch button or chip is pressed (the touch buttons
  are flat, so you can't feel them)
* confirmation of a panic
* "feel the kick": a pulse on chosen notes, e.g. the kick drum on channel 10
* right or wrong answer in the ear trainer (stage 7)
* low battery

The motor is switched through the power chip over I2C, so it's driven from
the UI thread with a few milliseconds of latency. That's fine for cues, but
not sample-accurate. It also buzzes audibly, which may matter near a mic.

**Plan (2026-11-01):** cues are produced in the MIDI task (beats scheduled one
period ahead, minus the motor's spin-up time) and played on the UI thread,
because the motor sits on the internal I2C bus. Cues differ by length and
rhythm: beat 70 ms, downbeat 140 ms, double and triple pulses for warnings.
There's also an optional speaker click per cue group, settings in a "Cues"
view on the Clock page, and expected accuracy of about ±5 ms.
**Implemented (2026-11-02):** `src/haptics.*` plays cues from the shared queue
(`src/cue_queue.h`) on the UI thread: patterns from `src/haptic_patterns.h`,
priorities, motor writes only when the level changes, and a speaker click per
cue group. The beat listener in the MIDI task schedules each next beat one
period ahead minus the lead time. `Ui::pushContent()` services cues between
its slices. The settings are a third view on the Clock page (C: Main →
Outputs → Cues), saved in NVS under `haptic`. Defaults until you decide:
**Cues are off by default** (master switch on the Cues view, or hold C on the Clock page;
decided 2026-11-02). Once switched on: metronome, clock alerts, phrase, panic, answers and battery vibrate; kick and
touch tick off; kick note 36 on channel 10; count-in off.

Differences from the plan:

* Clock lost / locked are detected on the UI side from the engine's snapshot
  (locked only counts after a loss), not posted by the listener.
* The count-in runs on the Clock page while it's open (1 or 2 bars at the
  set tempo, internal clock only, no MIDI sent); leaving the page cancels it.
  It uses the Metronome group's output.
* Low battery is checked every 30 s (below 10%, not charging), once per drop.
* The motor's spin-up time hasn't been measured with the IMU yet; the lead
  defaults to 25 ms and can be set on the Cues view (0–50 ms).

**Full plan:** [docs/stages/stage-10.md](docs/stages/stage-10.md)

### Stage 11: MIDI effects

Arpeggiator, chord generator, scale quantiser, transpose, velocity curves and
CC scaling. They're applied in the merge path between input and output, so
they combine with the router's settings.

**Implemented (2026-11-02)**, with the plan's defaults (chains per input, no default scale,
no CCs configured, arp passes notes through while the transport is stopped):

* `src/fx_engine.*` runs in the MIDI task. `Router::route()` hands channel messages to the
  input's chain after the filter and forced channel, but only while the chain has something
  to do (a slot on, or notes from earlier still needing their note-offs). Otherwise the
  router's path is unchanged, and a test checks the output is byte-identical with all
  effects off. What the chain produces goes to the input's destinations with the input's
  origin, so the note tracker, panic and routing changes work as before (a routing change
  also clears the chain's memory).
* Each input note remembers the notes it produced (128 per input); its note-off ends exactly
  those, even after settings changed or the slot was switched off.
* Arp on the clock engine's Step events (swing applies): 1/4, 1/8, 1/16, 1/32; up, down,
  up-down, random, as played; 1–4 octaves; gate; latch. Gates and the 1/32 offbeat are timed
  in the MIDI task's loop (1 ms resolution) rather than the clock scheduler, so they don't use
  scheduler slots.
* FX page (after Router): USB / DIN tabs, preset chip (tap: load the next of 8 presets), one
  row per slot (tap the box: on/off; tap the text: select, again: next setting), A/C −/+,
  B bypass, hold B save preset. LEDs 0–5 show the shown input's active slots. Settings in NVS
  under `fx` (v1), saved only while the transport is stopped.

Differences from the plan:

* One CC mapping per input instead of 8; no curve on CC mapping (min/max/invert only).
* No triplet arp rates; no free-run option while stopped.
* Stop (also sent by panic) ends the arp **and** its latched notes.
* Switching the arp off ends its sounding note, but keys still held don't start sounding
  normally; they play again from the next key press.
* Scales are the 8 from `music_theory` (no mixolydian yet); chord types: maj, min, 7, min7,
  sus4, 5th, octave, diatonic (from the scale).
* No arp-step LED flash, no FX BYP marker in the header (the footer shows "Bypassed").

**Plan (2026-11-01):** Per-input effect chains after the router's filters: transpose → scale quantise → chord → arpeggiator (clock-synced, latch) → velocity curve → CC scale/smoothing. Each input note remembers what it produced, so note-offs are always right, even when settings change. FX page, 8 presets.
**Full plan:** [docs/stages/stage-11.md](docs/stages/stage-11.md)

### Stage 12: Generative sequencers

Euclidean rhythms, with the hits shown around the "ring" of 10 LEDs. A
looping random sequence that slowly mutates.

**Plan (2026-11-01):** a Gen page with 8 slots running alongside the drum
machine:
* **Euclid:** steps, hits, rotation, accents, probability; each slot has its
  own length (polymeter).
* **Turing:** a mutating register, quantised to a scale from
  `music_theory`, with lock and reseed.
* **Mod lanes:** CC with S&H, smooth random or LFO shapes.

Euclid can also fill a drum lane. Shared note handling moves to
`src/step_voice.*`.
**Full plan:** [docs/stages/stage-12.md](docs/stages/stage-12.md)

**Implemented (2026-11-02):**

* **Gen page** (after Drums), 8 slots. The list shows each slot's kind, channel, a live
  preview (Euclid steps with the playhead, Turing's next notes, the mod value) and a mute
  chip. Tap the kind to cycle off / EUC / TUR / MOD, tap the preview to edit. The edit view is
  a two-column table of the slot's settings (tap one, A / C = − / +).
* **Euclid:** steps 1–32, hits, rotation, note, velocity, gate, probability, and an accent
  Euclid (+27 velocity). Slots loop over their own length (polymeter).
* **Turing:** a 2–16 value register with mutation (0 % = repeats exactly), lock, reseed,
  scale (8 scales added to `music_theory` with `quantise()`), root, octave, range and
  density (rests from the same register). Seeded xorshift32, so a seed reproduces its loop.
* **Mod lanes:** CC with S&H, smooth random, sine, triangle, ramps or square over 1 step to
  64 bars, min / max (inverted when max < min), sent only when the value changes.
* **Rates:** 1/16, 1/8 and 1/4 follow the clock's swung steps; 1/32 and 1/16T count clock
  ticks and stay straight.
* Transport comes from the clock. Stop releases every generator note; mod lanes stay where
  they are. Hold A realigns all slots on the next bar.
* **Drums page, Pattern view:** an Euclid fill for the lane selected in the Grid or Sound
  view (`−` / `+` set the hits, tap the middle to write them; accents on hits are kept).
* LEDs while the page is open: the Euclid ring (≤ 10 steps all shown, else the 10 around the
  playhead), Turing's current note colour plus lock state, or a mod-value bar.
* Settings are saved in NVS (`gen`, v1) 2 s after the last edit, only while stopped.
* **Defaults until the open questions are answered:** every slot starts muted; slot 1 is a
  four-on-the-floor Euclid on channel 10 note 36, slot 2 hats (7 over 16, note 42), slot 3 a
  Turing sequence on channel 3 (C minor, octave 3), slot 4 a sine mod lane on channel 3 CC 74;
  slots 5–8 are off. All output to DIN.
* **Deviations from the plan:** no Euclid ring drawing on screen (the LED bar shows it);
  no "Write" (stored loop) action: only the seed is saved, so a mutated loop isn't kept
  across power cycles; no undo for the drum Euclid fill, and it uses rotation 0; 1/16T and
  1/32 aren't swung.

### Stage 13: Recorder / looper

Record the merged stream into PSRAM, then save it to the SD card as `.mid`
files and play it back. The LEDs pulse red while recording.

**Plan (2026-11-01):** a clock-synced looper first (1–16 bars from the next
bar line, overdub with undo, optional quantise), a free recorder second. It
captures routed input from a MIDI-task observer into PSRAM at 96 PPQN.
Standard MIDI Files go to the SD card from a low-priority task, at most 4 KB
per hold of the shared SPI bus. Without a card everything except save and load
works. Needs a microSD card in the Core2.
**Measured 2026-11-02** (8 GB-class SDHC card, FAT32, Diag system view line "SD"): a 4 KB write
plus flush holds the bus for ~12.5 ms and a 4 KB read for ~2.6 ms. So write in ~1 KB chunks
(~3 ms) while MIDI runs, and use 4 KB chunks only when the transport is stopped.
**Implemented (2026-11-02): the looper.** Differences from the plan:

* **The free recorder isn't built yet:** only the clock-synced looper is.
* **Recording:** routed input only (the InputHooks observer ignores messages that weren't
  forwarded). Program changes aren't recorded. Input filter: USB/DIN and one channel or all.
  "Notes up to a sixteenth early count as the downbeat" isn't built.
* **Passes:** a pass collects in a pending list and joins the loop at each loop end, so a
  pass is heard from the next cycle on. Notes held over the end are cut there and, while
  overdubbing, restarted at the start. Up to 16 passes; undo removes the last one.
* **Start:** on the next bar line, or (chip "Phrase") the next phrase of stage 22.
  "Mute while overdubbing" isn't built.
* **Files:** Save writes `/oximate/loops/loopNNN.mid` (type 1 with one track per channel when
  the loop uses several channels, else type 0; 96 PPQN, tempo and time signature). Load lists
  that folder; a loaded loop keeps its own length and joins at the next bar. The clock's
  tempo is not changed by loading.
* **SD access:** a low-priority task on core 1 (`src/sd_store.*`) writes 1 KB chunks while
  the clock runs and 4 KB when stopped, each under the SPI bus lock. Closing a file (the
  FAT update) is the longest hold.
* **LEDs:** only while the Loop page is open (not "red on every page while recording").

**Full plan:** [docs/stages/stage-13.md](docs/stages/stage-13.md)

### Stage 14: SysEx librarian

Send `.syx` files from the SD card and capture SysEx dumps from devices. The
Subharmonicon files in `subH_sysex_user/` are the first test case.

**Plan (2026-11-01):** Send `.syx` files from SD with gaps between messages (default 20 ms), streaming past the 1024-byte SysEx limit. Capture dumps from DIN or USB into PSRAM, ended after 2 s of silence and saved byte-exact. Every send names its output, and there's a warning before universal SysEx. Test case: the Subharmonicon files.
**Implemented (2026-11-02)**, with these differences from the plan:

* **Sending:** `.syx` files from `/syx` (or saved dumps from `/oximate/dumps`) are
  loaded into PSRAM (up to 512 KB, 1024 messages) and split into messages;
  stray bytes and an unfinished last message are skipped and reported. The
  send job (`src/sysex_sender.*`) runs in the MIDI task and streams any length:
  * DIN through `DinMidi::sendRaw()`: from F0 to F7 other non-realtime DIN
    output waits in a 512-byte hold queue, clock still goes first;
  * USB through `UsbMidi::sendSysExPart()`: ready-made USB-MIDI packets
    (CIN 4, ending with 5/6/7) in the existing send queue.

  The gap between messages (0–250 ms, default 20) is timed in the MIDI task
  loop (1 ms resolution), not with the clock scheduler. Cancel and panic stop
  after the current F7. Every send names its output (DIN, USB or both; the
  page shows it before sending), and a broadcast warning appears for universal
  SysEx to device 7F.
* **Capture:** an `InputHooks` SysEx tap gets the raw bytes from both parsers,
  past the 1024-byte assembly limit, into a 512 KB PSRAM buffer
  (`src/sysex_capture.*`). It ends 2 s after the last byte, on Stop, or when
  the buffer is full. "Capture only" (on by default) keeps SysEx from a
  capturing input from being routed. Bytes arriving while (and 300 ms after)
  we send are ignored, so a device echoing our own SysEx isn't captured.
  Saved as `/oximate/dumps/<maker>-YYYYMMDD-HHMMSS.syx` (RTC set) or
  `<maker>-NNNN.syx`. Hold B sends a capture back from PSRAM, also without an
  SD card.
* **Not built:** sub-folders in the library, "Send + capture" with request
  files, remembering the output per folder (one output setting for all), a
  file-size column in the list, and a settable clock. Buffers are 512 KB each
  instead of 1 MB, to leave PSRAM for the looper.
* Settings in NVS under `syx` (v1). The Subharmonicon files from
  `subH_sysex_user/` go into `/syx` on the card (not in the repo).

**Full plan:** [docs/stages/stage-14.md](docs/stages/stage-14.md)

### Stage 15: Setlist / scenes

Send program changes and CC snapshots to several devices with one touch.

**Plan (2026-11-01):** Scenes of program changes and CC snapshots per device, plus optional OxiMate snapshots (tempo, routing, synth, drums, FX). Recall now, at the next bar or at the next phrase. Program changes are off per device by default (Elektron safety), with an optional lead time. Live view: A previous, B recall, C next.

**Implemented (2026-11-02):** `src/scenes.*` (engine) and `src/scene_page.*` (the "Set" page,
after Phrase).
* **Devices** (up to 8, Devices view): name, DIN/USB, channel, **PC allowed (off by default)**,
  PC shown as 1-128 or 0-127, lead time 0–4 beats. A scene can't hold a PC for a device that
  doesn't allow it, and the recall strips such PCs again before sending.
* **Scenes** (up to 32): name (from a list), timing **next bar** (default), now or next phrase,
  per device bank MSB/LSB + PC and up to 16 CCs, and OxiMate parts: tempo, a router + synth
  snapshot (8 shared slots), drum pattern, FX preset and which Gen slots play.
* **Recall** runs in the MIDI task: lead-time PCs go out that many beats before the bar or
  phrase boundary, the other PCs at the boundary, then the UI applies the OxiMate parts through
  each module's own `changed()` (so the router's release-before-change and SamSynth's diffing
  apply; the router's LED level and the synth's output/wiring/config channel stay as they are),
  then CC snapshots go out 4 messages per 10 ms. Stopped transport: recalls happen at once.
  Panic cancels a pending recall.
* **Live view:** now / next scene, countdown in beats (and on the LEDs), the following scenes,
  Devices / New scene / Cancel. Tap "Now" or "Next" to edit that scene.
* **Editor:** name, timing, tempo (tap clears); PC per device (A/C, `!` = may switch patterns),
  CC count (tap the selected CC cell again to clear); Capture (tempo, router, synth, drum
  pattern, FX preset, Gen mutes), Clear, Learn (CCs arriving on a device's channel are stored),
  New, Delete.
* Saved in NVS (header key `scenes`, one key per scene `scn00`..`scn31`, snapshots `ssn0`..`ssn7`),
  only while the transport is stopped.
* **Differences from the plan:** no "next drum pattern end" timing and no per-device SysEx; the
  OxiMate parts are applied by the UI a few ms after the boundary (tempo from the next clock tick);
  scene names come from a fixed list (no text entry); the SD/JSON setlist files are a later hook
  (`Scenes::load()/save()` are the place to add them).
**Full plan:** [docs/stages/stage-15.md](docs/stages/stage-15.md)

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
  before a router change takes effect. Long notes are never cut on a timer.
* **Diagnostics page:** channel activity, message counters and rates, DIN
  TX buffer, dropped SysEx, USB errors, heap/PSRAM, battery.

**Implemented (2026-11-01)**, with these differences from the plan:

* Level 3 sends ordinary 3-byte note-offs (about 2 s on DIN) instead of a
  running-status stream (1.3 s), so other output never has to pause.
* Notes are not released on a timer: the 30 s release was removed after
  hardware testing, because long pads are held on purpose.
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

**Plan (2026-11-01):** BLE MIDI peripheral (iPad/Mac) on NimBLE 2.x, using our own thin transport because the installed BLE-MIDI library doesn't match NimBLE 2.x. The MIDI task moves to core 1 while a radio is on. Also covers the shared groundwork: more inputs and outputs, a route matrix (RouterConfig v3), and a Radio page with a mode chosen at boot.

**Implemented (2026-11-05)**, decisions of 2026-11-05 applied:

* **Radio page** (before Diag): mode Off / BLE / Wi-Fi (saved, A reboots into it), the running
  radio's status, its four routes (BLE>DIN, BLE>USB, DIN>BLE, USB>BLE; all on by default), and
  the option chip (BLE: Central on/off; Wi-Fi: own access point / join network).
* **Peripheral, always:** OxiMate advertises as "OxiMate" with the BLE MIDI service; iPad (an
  app's Bluetooth MIDI dialog) and Mac (Audio MIDI Setup → Bluetooth) connect. Up to two hosts.
* **Central, optional** (for a BLE controller or the Oxi One): B Devices scans, a tap connects
  and remembers the device, and OxiMate reconnects to it whenever it is around. C forgets it.
* **No clock over radio** in either direction: clock and Start/Stop/Continue are dropped.
* Own packet codec (`src/ble_midi_codec.*`, host-tested: timestamps, running status, SysEx across
  packets, real-time inside messages), packets sized to the negotiated MTU.
* The MIDI task runs on core 1 while a radio is on (the BLE/Wi-Fi stacks own core 0).

Differences from the plan:

* **Two firmwares:** the BLE controller and the Wi-Fi stack don't fit in the ESP32's IRAM
  together with the precompiled Arduino core (3.4 KB over). `make build`/`make send` is the BLE
  firmware (app slot 0); `make build-wifi`/`make send-wifi` the Wi-Fi firmware (slot 1). Settings
  are shared; choosing the other radio and A boots the other slot (or says it isn't installed).
* NimBLE-Arduino 2.5.0 is pinned in `libraries/` by the Makefile (the sketchbook copy was
  incomplete). OxiMate tells the core it uses BLE (`bleInUse()`) only in BLE mode, so in the other
  modes the controller's RAM goes to the heap.
* To make room in internal RAM: `NoteTracker::release()` no longer copies all held notes into a
  16 KB static buffer, and the SD check's buffers are on the heap.
* Messages from all connected BLE devices arrive as input BLE; BLE→BLE is never routed (an iPad
  and the Oxi One connected together don't talk to each other through OxiMate).
* BLE timestamps are decoded but not used for scheduling (arrival time is used).
* No pairing/bonding; no BLE connection-interval display (requested 7.5–15 ms).

**Full plan:** [docs/stages/stage-17.md](docs/stages/stage-17.md)

### Stage 18: Network MIDI

RTP-MIDI / AppleMIDI over Wi-Fi. This probably can't run at the same time as
BLE MIDI (see Architecture).

**Plan (2026-11-01):** RTP-MIDI/AppleMIDI, found by the Mac's Audio MIDI Setup over mDNS. OxiMate is its own access point by default, or joins a studio network. Wi-Fi power save is off for latency, which costs about 100 mA. Only one radio runs at a time; switching reboots.

**Implemented (2026-11-05)** (the Wi-Fi firmware, see stage 17 for the two firmwares):

* **Own access point by default:** "OxiMate-xxxx", WPA2, password `oxi-` + 8 hex digits derived
  from the MAC address (both on the Radio page); OxiMate is 192.168.4.1.
* **Join a network (option):** on the access point, open `http://192.168.4.1` on a phone or the
  Mac, pick the network and type the password; OxiMate saves it and reboots into it. If it can't
  join within 20 s it opens its own access point again. C on the Radio page forgets the network.
* **Session:** mDNS `_apple-midi._udp` "OxiMate" on ports 5004/5005; the Mac (Audio MIDI Setup →
  Network) or iPad starts the session, OxiMate answers. One session at a time (another host gets
  NO). Clock sync answered; latency (half the sync round trip), packets and lost packets on the
  Radio page. Session end (BY, or no sync for 60 s) releases held notes like an unplug.
* **No clock** over the network; network input is routed to DIN and USB by default.
* **Power save off** while network MIDI runs.

Differences from the plan:

* Own minimal AppleMIDI + RTP-MIDI implementation (`src/rtp_midi.*`, host-tested) instead of the
  lathoub library: OxiMate never invites, sends without a recovery journal (J = 0) and ignores the
  journal of what it receives (a lost packet is lost; counted as "lost").
* The setup page is always on in access-point mode (no separate Setup mode, no captive DNS) and
  there is no on-screen keyboard.
* No clock to/from the network (decided 2026-11-05; the plan had clock on by default).
* To fit the Wi-Fi stack's static RAM, the Analyse page's statistics copy (8 KB) moved to the heap.
* No mDNS check on the access point network yet: if the Mac doesn't list OxiMate, add it by hand
  in Audio MIDI Setup → Network (192.168.4.1, port 5004).

**Full plan:** [docs/stages/stage-18.md](docs/stages/stage-18.md)

### Stage 19: Extra controls

Add M5 Grove units (8-Encoder, Dual Button, Angle) as physical knobs and
buttons alongside the touch buttons.

**Plan (2026-11-01):** Grove units on Port A (its own I2C bus, separate from the internal one): 8Encoder recommended, 8Angle, Joystick2; Fader or Dual Button on Port B. Polled from an I/O task on core 1. The 8Encoder's switch selects page-control mode or learnable MIDI CC mode. Shares the bus with the stage 20 CV board.
**Full plan:** [docs/stages/stage-19.md](docs/stages/stage-19.md)

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

**Plan (2026-11-02):** from the DFAM manual:
* ADV/CLOCK steps on each rising edge, and RUN/STOP doesn't reset, so
  reaching a step means counting and pulsing.
* The step burst holds velocity CV at 0 V, then fires TRIGGER at the note's
  velocity, so skipped steps stay silent.

Hardware: a ~€25–35 CV board on Port A:
* MCP4728 DAC at 0x60 with TLV2374 op-amps, giving about ±4.9 V from a
  −5 V charge pump;
* 74AHCT125 gate buffers.

Gates on G26 (ADV/CLOCK) and G27 (TRIGGER), with an optional step-follower
input on G36. It adds a gate output to the clock engine, a DFAM page and
multimeter calibration. Milestone 0 is a single buffered gate, to check
whether skipped steps can be heard.
**Full plan:** [docs/stages/stage-20.md](docs/stages/stage-20.md)

### Stage 21: Chord detector

A page that names the chord being played into OxiMate, from a USB keyboard,
the Oxi One, the M8 or an Elektron sequencer.

* **Held notes:** a `ChordInput` observer in the MIDI task keeps the exact
  set of notes held at the inputs (USB, DIN or both; all channels or one),
  with optional sustain-pedal handling. It doesn't use the UI event ring,
  which can drop events and leave a note shown as held.
* **Naming:** shared `music_theory` (with stage 7), extended with 6, 9, 11,
  13 and altered chords, a matcher that tolerates missing or extra notes,
  inversions, slash chords, ranked alternatives, and an optional key
  estimate.
* **Strums and arpeggios:** a 40 ms settle time for strummed chords, and an
  optional arpeggio window (250/500/1000 ms or 1 bar) that collects
  arpeggiated notes into one chord.
* **Page:** big chord name and inversion, the notes coloured by pitch class,
  up to 2 alternatives, the last 4 chords, and the key estimate. **B**
  switches to a simple view with only the current chord, large. A clears
  (hold A: freeze), C sharps/flats. The LEDs show the chord tones in their
  pitch-class colours while the page is open.
* **Defaults (decided):** sharps; channel 10 (drums) excluded. Once stage 3
  runs, the chord name also appears on the Oxi One's screen.

**Implemented (2026-11-01)**, with these differences from the plan:

* `music_theory` gained a second chord table for lead-sheet names (37 types:
  power, triads, sus, 6ths, 7ths, added tones, 9ths, 11ths, 13ths, 7b5,
  7b9, 7#9, 7#11, 7b13, 13b9) beside stage 7's exact-match table, which is
  unchanged. The ear page's explorer still uses the stage 7 matcher.
* Scoring weights as planned, except: a missing required tone costs −25
  (plan: −12), so a name that claims an unplayed 7th loses against a plain
  chord with one extra note (`C/F#`, not `C7#11/F#`); and 6 / m6 chords need
  their 5th (otherwise `Eb6` swallows `Cm/Eb`).
* Equivalent spellings are named after the bass: Eb G Bb C over Eb is `Eb6`,
  over C it's `Cm7`. Only roots that are played are considered (no rootless
  voicings). `7alt` isn't in the table.
* Two pitch classes show as an interval (`C+E`, "M3") unless they form a
  power chord (`C5`).
* Detection runs on the UI thread on every loop, also while the page is
  hidden, so the history and key estimate keep up. `ChordDetector::
  stableName()` returns the current stable chord name: stage 3 will show it
  on the Oxi One's screen.
* Held notes are also cleared after a completed panic and with A on the
  page (manual unstick).

**Full plan:** [docs/stages/stage-21.md](docs/stages/stage-21.md)

### Stage 22: Phrase tracker

Keeps track of phrasing (usually 8 or 16 bars) when playing live, dawless.
Its own page, built on the stage 8 clock engine, which already counts bars
from Start in both internal and follow mode.

* **Counting:** bar within the phrase (phrase length 4, 8, 16 or 32 bars),
  and optionally a higher level ("phrase 3 of 4" in a 64-bar section).
* **Re-sync:** a "bar 1 now" button, tapped on a downbeat, realigns the count
  when the master was already running or OxiMate missed its Start. Song
  Position Pointer from the master realigns it automatically.
* **Display:** big `9 / 16` counter, a phrase progress bar, and a countdown
  ("4 bars left"). Optionally named sections ("intro, build, drop") with "drop
  in 4 bars".
* **Cues without looking:**
  * **LEDs:** the 10 LEDs fill across the phrase, change to a warning colour
    in the last bar or two (time for a fill or transition), and flash on
    bar 1.
  * **Vibration:** a pulse on each phrase start, and optionally a double
    pulse one bar before.
* **Optional, opt-in automation:** send a message at phrase boundaries, e.g.
  a program change that switches Elektron patterns (which they queue to the
  end of the current pattern).

Open questions: which box is the clock master live and does it send Start and
Song Position; phrase lengths needed; vibration cues and warning lead time;
named sections; program changes at boundaries.

**Plan (2026-11-01):** Counts bars from the clock engine (bar in phrase, phrase in section), with a "Bar 1" resync button, Song Position realign, and `?` while the phrase position is unknown. Big counter, progress bar and countdown; LEDs fill and warn before the phrase end; vibration cues through stage 10. Defaults assumed until answered: 16 bars, 4 phrases, 1-bar warning.
**Implemented (2026-11-02)** with the plan's assumed defaults (16-bar phrases,
4 phrases per section, 1-bar warning, LEDs on the page, Song Position
realigns, no program changes):

* `PhraseTracker` counts in the MIDI task from the clock engine's new
  `RawTick` event (every tick, also while stopped), Start, Stop, Continue and
  Locate. A master that was already playing is counted from its ticks and
  shown with a red `?` until you tap **Bar 1**. Internal clock while stopped
  doesn't count, and a master's Stop freezes the position.
* **Bar 1** (B) is rounded to the nearest bar line in the MIDI task; hold B
  restarts the phrase but keeps the section count. **A** makes the current
  bar bar 1 straight away.
* Phrase page: big counter (orange in the warning bars), `/16`, a segment per
  bar (warning bars orange), bars left, phrase in section, a hint while not
  synced; chips for length, section, warning, Song Position and LEDs; C
  switches to a big view (counter and bar only).
* LEDs (while the page is open): segments fill across the phrase, blue,
  orange in the warning bars, white flash on bar 1, blinking while not
  synced.
* Cues: `Cue::PhraseEnding` and `Cue::PhraseStart` are posted to the shared
  cue queue one bar ahead, with the downbeat's time as `dueMicros` (stage 10
  subtracts the motor's spin-up), only while synced.
* Hooks for stages 13/15: `PhraseTracker::addBoundaryListener()` (called in
  the MIDI task on every phrase start) and `PhraseSnapshot::nextPhraseTick`.
* Settings saved in NVS (`phrase`, v1), only while stopped.
* **Not built yet:** named sections, the opt-in program change at phrase
  boundaries, "LEDs always", and the phrase counter in the header. A clears
  immediately instead of "at the next Start".

**Full plan:** [docs/stages/stage-22.md](docs/stages/stage-22.md)

## Build and upload

Requires `arduino-cli` with the `esp32:esp32` core, plus the **M5Unified** and
**M5GFX** libraries.

```sh
make build                               # fetches + patches USB Host Shield 2.0 on first run
make send PORT=/dev/cu.usbserial-XXXX    # upload
make                                     # build + send
make log SECONDS=20                      # reset the board and show its serial output
make build-wifi && make send-wifi        # the Wi-Fi firmware into app slot 1 (stage 18)
```

`make send` writes the BLE firmware to app slot 0 and boots it; `make send-wifi` writes the
Wi-Fi firmware to slot 1 without changing which slot boots. The Radio page switches between them.

### Unit tests

```sh
make test     # host build of the parsers, router, note tracker, output queues and event ring
```

The tests compile the hardware-independent modules on the Mac against the
stand-ins in `test/stubs/`. Shared helpers live in `test/test_support.h`. Each
stage adds a `test/test_<stage>.cpp` with `TEST(name) { ... }` blocks (they
register themselves), and lists its host-testable sources in
`test/sources_<stage>.mk` as `TEST_SOURCES += src/...cpp`.

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
| `src/midi_outputs.*`| the single send path (DIN, USB, BLE, NET), with counters |
| `src/note_tracker.*`| held and sustained notes per output, as sent |
| `src/spi_bus.*`     | lock for the SPI bus shared by display, USB host and SD |
| `src/sd_check.*`    | SD card check at boot (type, size, 4 KB write/read time on the shared bus) |
| `src/sync.h`        | small critical sections between the two cores |
| `src/midi_message.h`| `MidiMessage`, the common type for complete messages |
| `src/usb_midi.*`    | USB host: USB-MIDI packets ⇄ messages, including SysEx assembly |
| `src/din_midi.*`    | DIN serial: byte stream (running status, realtime) ⇄ messages |
| `src/monitor.*`     | the Monitor page |
| `src/router.*`      | filtering, remapping, routing; settings saved in NVS |
| `src/router_page.*` | the Router page |
| `src/fx_engine.*`   | MIDI effects per input (transpose, scale, chord, arp, velocity, CC), note mapping, presets |
| `src/fx_page.*`     | the FX page |
| `src/leds.*`        | GO Bottom2 LED bar |
| `src/sam_synth.*`  | SAM2695 control: part map, layers, effects, auditions, restore after resets |
| `src/synth_page.*`  | the Synth page and instrument browser |
| `src/drum_machine.*` | drum machine: patterns, playback on clock steps, pad controller input |
| `src/drum_page.*`   | the Drums page (Grid, Sound and Pattern views) |
| `src/step_voice.*`  | note-on, gated note-off via the clock scheduler, release on stop (drums and Gen) |
| `src/euclid.h`      | Euclidean rhythm masks |
| `src/gen_core.*`    | seeded RNG, Turing register, mod-lane shapes (pure, host-tested) |
| `src/gen_engine.*`  | stage 12 generators: 8 slots on the clock in the MIDI task |
| `src/gen_page.*`    | the Gen page (slot list and edit view) |
| `src/drum_kits.h`   | GM percussion note names |
| `src/gm_names.h`    | GM instrument names, families, drum kits |
| `src/music_theory.*` | note names, intervals, chords, scales; exact matching (stage 7), scored extended-chord matching and key estimate (stage 21) |
| `src/ear_trainer.*` | ear-trainer logic: questions, answers, levels, statistics |
| `src/ear_page.*`    | the Ear page (trainer and chord explorer) |
| `src/chord_input.*` | stage 21: notes held at the inputs (input observer in the MIDI task) |
| `src/chord_detector.*` | stage 21: settle, name, alternatives, history, key estimate (UI side) |
| `src/chord_page.*`  | the Chord page |
| `src/phrase_tracker.*` | phrase counting in the MIDI task (bars, phrases, sections, realign, Song Position, cues, boundary hooks) |
| `src/phrase_page.*` | the Phrase page |
| `src/scenes.*`      | setlist and scenes: devices, recall engine (MIDI task), snapshots, NVS |
| `src/scene_page.*`  | the Set page: live view, scene editor, device setup |
| `src/loop_buffer.*` | looper event storage: passes, merge per cycle, quantise, undo |
| `src/looper.*`      | clock-synced looper in the MIDI task (capture observer, playback, note safety) |
| `src/looper_page.*` | the Loop page and its file list |
| `src/smf.*`         | Standard MIDI File writer and reader (type 0/1, rescaled to 96 PPQN) |
| `src/sd_store.*`    | SD task on core 1: chunked file reads/writes under the bus lock, folder listing |
| `src/syx_file.*`    | .syx splitting, manufacturer names, broadcast check, dump file names, USB SysEx packer |
| `src/sysex_sender.*` | SysEx send job in the MIDI task: streaming to DIN/USB, gaps, cancel |
| `src/sysex_capture.*` | SysEx capture from the input tap into PSRAM, idle end, capture-only |
| `src/sysex_page.*`  | the SysEx page (library, send, capture) |
| `src/speaker_synth.*` | 8-voice synth on the Core2's built-in speaker |
| `src/local_sound.h` | copies generated notes (Gen, looper) to the speaker in speaker mode |
| `src/clock_engine.*` | internal/followed clock, transport, swing, output ratios, scheduler |
| `src/clock_page.*`  | the Clock page (tempo, source, swing, outputs, cue settings, count-in, LED beat display, IMU tap) |
| `src/haptics.*`     | stage 10 cue player: vibration patterns, priorities, scheduled beats, speaker click; clock listener and kick observer |
| `src/haptic_patterns.h` | the vibration pattern table |
| `src/cue_queue.h`   | cue list and the queue any task posts cues to |
| `src/clock_stats.*` | clock statistics per input (intervals, jitter, drift, dropouts, transport) and USB-vs-DIN phase |
| `src/clock_analyser.*` | feeds the statistics from an input observer; copies for the UI; CSV queue |
| `src/analyser_page.*` | the Analyse page (Stats, Histogram, Drift, Phase) |
| `src/tap_tempo.h`   | tap tempo averaging |
| `src/platform_time.h` | 64-bit microsecond clock (esp_timer) |
| `src/ui.*`          | page switching, header and footer |
| `src/panic.*`       | panic levels 1–3, automatic release (USB unplug, Active Sensing, stuck notes) |
| `src/diagnostics.h` | input statistics |
| `src/diag_page.*`   | the Diag page |
| `src/radio.*`       | radio mode and settings (NVS), `RadioTransport`, the two-firmware slot switch |
| `src/radio_page.*`  | the Radio page (mode, status, routes, BLE device scan) |
| `src/packet_ring.*` | SPSC ring of packets between a radio stack and the MIDI task |
| `src/ble_midi_codec.*` | BLE MIDI packet decoder and encoder (pure, host-tested) |
| `src/ble_midi.*`    | BLE MIDI on NimBLE-Arduino 2.x: peripheral, central, scan (BLE firmware) |
| `src/rtp_midi.*`    | RTP-MIDI codec and AppleMIDI session (pure, host-tested) |
| `src/net_midi.*`    | Wi-Fi access point / station, UDP, mDNS, setup page, network task (Wi-Fi firmware) |

## Credits

Based on [MIDIDump](https://github.com/gdsports/MIDIDump) by gdsports, which
is MIT licensed.
