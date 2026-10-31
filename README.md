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
| 3 | [Oxi One remote](#stage-3-oxi-one-remote) | — | planned |
| 4 | [Push 2 host](#stage-4-push-2-host) | — | planned |
| 5 | [Drum machine](#stage-5-drum-machine) | — | idea |
| 6 | [Preview synth](#stage-6-preview-synth) | — | idea |
| 7 | [Ear trainer](#stage-7-ear-trainer) | — | idea |
| 8 | [Clock master](#stage-8-clock-master) | — | idea |
| 9 | [Clock analyser](#stage-9-clock-analyser) | — | idea |
| 10 | [Haptic metronome](#stage-10-haptic-metronome) | — | idea |
| 11 | [MIDI effects](#stage-11-midi-effects) | — | idea |
| 12 | [Generative sequencers](#stage-12-generative-sequencers) | — | idea |
| 13 | [Recorder / looper](#stage-13-recorder--looper) | — | idea |
| 14 | [SysEx librarian](#stage-14-sysex-librarian) | — | idea |
| 15 | [Setlist / scenes](#stage-15-setlist--scenes) | — | idea |
| 16 | [Panic & diagnostics](#stage-16-panic--diagnostics) | — | idea |
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
  at 31250 baud.
* **DIN IN always reaches the Core2**, in both switch positions.
* **The Core2's output always drives the SAM2695 synth.**
* The front switch only selects the source for **DIN OUT**:
  * **Separate / "from core"**: DIN OUT carries the Core2's output. **Use this
    setting for OxiMate.** DIN OUT then carries the merged stream.
  * **Bypass**: DIN OUT is a hardware thru of DIN IN. The Core2's output only
    reaches the SAM2695, so USB MIDI does not reach DIN OUT.

### About the Core2's own USB-C port

The Core2's USB-C port is a USB-serial bridge (CH9102), not a USB MIDI
device. It can't appear as a MIDI port on a computer. All USB MIDI goes through
the USB Host Module.

## Architecture

These rules apply to every stage.

* **Pages and modes.** The UI is a set of pages, such as Monitor, Router and,
  later, Drum machine and Push 2. MIDI merging and routing keep running in the
  background whichever page is shown. Later stages add pages, and a mode menu
  once there are more than a few.
* **Buttons.**
  * **Tap the header bar** (top of the screen) to switch to the next page.
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
│ [Clk both][SysEx on][USB>keep][DIN>keep]
│   All ch       LEDs:med     Defaults   │  touch buttons A / B / C
└────────────────────────────────────────┘
```

* **Routes**: turn USB→DIN, DIN→DIN (software thru) and DIN→USB on or off
  individually.
* **Channel filter** per input: blocked channels are dropped before routing.
* **Clock**: forward clock and transport (F8, FA, FB, FC, F2) from both
  inputs, USB only, DIN only, or neither. Two clock sources merged together
  would double the tempo, so choose one when both inputs send clock.
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

* Goal: put the Oxi One into remote-control mode, update its display, and map
  its button and encoder input to MIDI commands for the M8.
* Open question: does remote mode replace the Oxi's normal behaviour, or run
  alongside it?
* Needed: the Oxi One remote-mode SysEx specification.

### Stage 4: Push 2 host

* The Push 2 has no standalone mode, so it needs a host. OxiMate would be that
  host.
* Pads, buttons, encoders, the touch strip and all LEDs work over standard USB
  MIDI. The protocol is documented in Ableton's `push-interface` repository on
  GitHub.
  * The 8×8 pads are notes 36–99 with velocity and aftertouch. Set an LED's
    colour by sending a note-on whose velocity picks a palette entry.
  * Buttons are CCs. Encoders send relative CCs and report touch. The touch
    strip sends pitch bend.
  * The Push has two ports, Live and User (USB cables), selected with SysEx.
* The Push's display is not driven over MIDI. Each frame is about 300 KB of
  pixel data (960×160) sent over a separate USB channel. The MAX3421E runs at
  USB full speed over SPI, so expect about 1 frame per second at best. Use the
  Core2 screen for status instead.
* The Push 2 needs its own power supply, because the USB host module can't
  power it.
* Plan: enumerate the Push and log its input → light LEDs on pad press → step
  sequencer → control-surface mode.

### Stage 5: Drum machine

A 16-step touch grid plays the SAM2695's General MIDI drum kit on channel 10.
The pattern is mirrored to DIN OUT so external drums can follow. The LED bar
shows the playhead and the bar position.

### Stage 6: Preview synth

Hear any incoming channel on the SAM2695: pick a channel and a GM program
(instrument) on screen. This includes setting the SAM2695 voice, which moved
here from stage 2. The synth chip and DIN OUT share the same output, so
program changes for the synth also reach external gear. Use a channel the
external gear ignores.

### Stage 7: Ear trainer

Play intervals and chords on the SAM2695 and answer on the touch screen. The
LEDs flash green for a correct answer and red for a wrong one.

### Stage 8: Clock master

Internal MIDI clock with tap tempo (touch the screen or tap the case, via the
IMU), swing, start/stop, and clock dividers or multipliers per output. The LED
bar pulses on each beat, in a different colour on the downbeat, and shows the
bar position.

### Stage 9: Clock analyser

Measure the jitter and drift of incoming MIDI clock: min/avg/max tick
interval and BPM drift. Useful for checking M8 or Oxi One sync. The LEDs show
green when stable, yellow when jittery and red when the clock drops out.

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

All Notes Off on every channel, a stuck-note detector, and per-channel
activity on the LEDs (channels 1–10).

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
```

### USB Host Shield 2.0 (patched)

On the first build, the `Makefile` clones USB Host Shield 2.0 at commit
`ef5b046` into `libraries/`. It then applies `USB_Host_Shield_2.0.patch`,
which sets the Core2 pin mapping above, and marks the version as
`1.7.0-core2patch`.

### Code layout

| File | Purpose |
|------|---------|
| `oximate.ino`       | setup and loop; wires inputs to router, clock tracker, LEDs and monitor |
| `src/midi_message.h`| `MidiMessage`, the common type for complete messages |
| `src/usb_midi.*`    | USB host: USB-MIDI packets ⇄ messages, including SysEx assembly |
| `src/din_midi.*`    | DIN serial: byte stream (running status, realtime) ⇄ messages |
| `src/monitor.*`     | the Monitor page |
| `src/router.*`      | filtering, remapping, routing; settings saved in NVS |
| `src/router_page.*` | the Router page |
| `src/leds.*`        | GO Bottom2 LED bar |
| `src/clock_tracker.h` | BPM and transport per input |
| `src/ui.*`          | page switching, header and footer |

## Credits

Based on [MIDIDump](https://github.com/gdsports/MIDIDump) by gdsports, which
is MIT licensed.
