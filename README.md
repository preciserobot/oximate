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

| # | Stage | Status |
|---|-------|--------|
| 1 | **MIDI monitor + merge**: show all incoming MIDI and merge USB and DIN inputs to DIN OUT | done, untested on hardware |
| 2 | **Router**: set up routing, channel filters and remapping, and the SAM2695 voice on the device | planned |
| 3 | **Oxi One remote**: put the Oxi One in remote mode and use it as a control surface for the M8 | planned |
| 4 | **Push 2 host**: use an Ableton Push 2 as a control surface and step sequencer, like the Oxi One | planned |

Stage 2 was originally "send USB MIDI to the attached MIDI port". Stage 1
already does that, so stage 2 is now about making the routing configurable.

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

## Stage 1: MIDI monitor and merge

### MIDI routing

Every input goes to every output, except back to the port it came from:

| From \ To   | DIN OUT + SAM2695 | USB device |
|-------------|:-----------------:|:----------:|
| USB device  | ✔                 | ✘ (no echo)|
| DIN IN      | ✔ (software thru) | ✔          |

* The routes are compile-time constants (`kRoute*`) in `oximate.ino`. Stage 2
  makes them configurable on the device.
* Both inputs are parsed into **complete messages** before they're forwarded,
  so the merged stream never interleaves two messages. SysEx is assembled in
  full (up to 1024 bytes; longer messages are dropped) and sent in one piece.
  Realtime bytes (clock, start/stop) pass through immediately.
* DIN input handles running status. Output never uses running status.
* USB cable numbers are kept, so DIN-to-USB messages go to cable 0.
* DIN carries about 3000 bytes per second. A USB device can send faster than
  that, so a 1 KB transmit buffer absorbs bursts. Sustained overload will slow
  the main loop down.

### Display

```
┌────────────────────────────────────────┐
│OxiMate USB 1C75:0288    DIN 120.0 PLAY │  header: USB device VID:PID, clock
│USB   1 NoteOn   C4   100               │
│USB   1 NoteOff  C4   0                 │  log: newest at the bottom
│DIN  10 CC       74   127               │  USB lines cyan, DIN lines orange
│USB2  1 Bend     +512                   │  USB2 = USB cable 2
│DIN  -- SysEx 6: 7E 7F 06 01            │
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

## Stage 3: Oxi One remote (notes)

* Goal: put the Oxi One into remote-control mode, update its display, and map
  its button and encoder input to MIDI commands for the M8.
* Open question: does remote mode replace the Oxi's normal behaviour, or run
  alongside it?
* Needed: the Oxi One remote-mode SysEx specification.

## Stage 4: Push 2 host (notes)

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

## More ideas for this hardware

Ideas that stay on the music and MIDI theme, using the LEDs, the touch screen,
the motion sensor and the SAM2695.

**With the SAM2695 synth (128 General MIDI sounds and a drum kit)**
* **Pocket drum machine**: a 16-step touch grid plays the GM drum kit on
  channel 10. The LED bar shows the playhead, and the pattern is also sent to
  DIN OUT.
* **Touch instrument**: an on-screen keyboard or XY pad mapped to note and
  filter cutoff. The IMU adds tilt-to-pitch-bend or modulation, and the
  vibration motor gives feedback on note-on.
* **Preview synth for the monitor**: hear any incoming channel on the SAM2695
  while debugging a rig, with the instrument chosen on screen.
* **Ear trainer or chord explorer**: play intervals or chords on the SAM2695
  and answer on the touch screen.

**Clock and timing**
* **Clock master**: tap tempo (touch or IMU tap), swing, start/stop, and clock
  dividers or multipliers per output. Pulse the LED bar on each beat and use a
  different colour on the downbeat.
* **Clock analyser**: measure the jitter and drift of incoming clock (useful
  for checking the M8 or Oxi sync).
* **Haptic metronome**: the vibration motor pulses on the beat, for
  headphone-free practice.

**MIDI effects in the merge path**
* Arpeggiator, chord generator, scale quantiser, transpose, velocity curves,
  note and channel filters, channel remapping, and CC smoothing or scaling.
* **Generative sequencers**: Euclidean rhythms, or a looping random sequence
  that slowly mutates.

**Storage and utilities (SD card)**
* **MIDI recorder and looper**: record the merged stream and save or play it
  back as `.mid` files.
* **SysEx librarian**: send `.syx` files from the SD card, such as the
  Subharmonicon files in `subH_sysex_user/`, and capture SysEx dumps from
  devices.
* **Setlist and scene recall**: send program changes and CC snapshots to
  multiple devices from one touch.
* **Panic and diagnostics**: All Notes Off on every channel, a stuck-note
  detector, and per-channel activity on the 10 LEDs.

**Wireless**
* **BLE MIDI bridge**: connect wirelessly to an iPad or Mac (the ESP32
  supports BLE MIDI).
* **Network MIDI (RTP-MIDI / AppleMIDI)** over Wi-Fi.

**More controls**
* Add M5 units over Grove, such as 8-Encoder, Dual Button or Angle units, for
  physical knobs and buttons alongside the touch buttons.

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
| `oximate.ino`       | setup and loop, MIDI routing, button handling |
| `src/midi_message.h`| `MidiMessage`, the common type for complete messages |
| `src/usb_midi.*`    | USB host: USB-MIDI packets ⇄ messages, including SysEx assembly |
| `src/din_midi.*`    | DIN serial: byte stream (running status, realtime) ⇄ messages |
| `src/monitor.*`     | the monitor display |

## Credits

Based on [MIDIDump](https://github.com/gdsports/MIDIDump) by gdsports, which
is MIT licensed.
