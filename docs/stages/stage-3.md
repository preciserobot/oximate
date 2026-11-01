# Stage 3: Oxi One remote

> **Status: plan, not implemented.** Written 2026-10-31. Where this plan and the README's
> [Shared infrastructure](../../README.md#shared-infrastructure) section disagree, the README wins:
>
> * Encoder CCs default to the M8's control channel (set on the Oxi page), not channel 16. Channel 16 is reserved for SAM2695 settings.
> * `MidiSource::Internal` / `Router::sendInternal()` → use the shared `MidiOutputs::send()` with an `Internal` origin.
> * `setOwner()`/`setPixel()` → use the shared LED claim API.
> * Consuming Oxi events in `onMidi()` → register as an input interceptor.
>
> **Decided 2026-11-01:**
>
> * Oxi One **MK1**: use model byte `00`. Check the firmware version in M0.
> * The Oxi's screen also shows the current chord from stage 21's chord detector (a small text API on `OxiRemote`).
> * M8 **Model:02** over **TRS type A** from OxiMate's MIDI OUT (check the Unit MIDI's TRS type). The control channel is still to be read off the M8.

## Goal

Use the Oxi One as a **control surface for the Dirtywave M8**, with OxiMate in
between:

```
 Oxi One ──USB──► OxiMate ──TRS/DIN OUT──► M8 (MIDI in)
 (remote mode)  ◄──LEDs/OLED  ◄──DIN IN──── M8 (clock, optional)
```

* OxiMate puts the Oxi into **remote mode**. In that mode the Oxi reports
  every pad, button and encoder action, and OxiMate controls its grid LEDs,
  button LEDs and OLED.
* OxiMate translates those events into M8 commands: transport, cursor keys,
  track mutes and solos, playing notes on the grid, and CCs from the encoders.
* The Oxi's events are consumed by the remote layer. They are **not** passed
  to the router, so they never reach the M8 as raw notes.

## Protocol (official, but not yet verified on hardware)

The source is OXI's **Remote Control Reference** spreadsheet
([Google Sheet](https://docs.google.com/spreadsheets/d/1Yccnrluv10QL_PauMmtCt64EtYjfSeZEXKrlrw8P24w)).
The manual links to it (p.273, per
[oxi-mcp](https://github.com/nkorai/oxi-mcp)), and the v4.5 user guide's
changelog lists "New MIDI Remote control implementation"
([guide](https://www.scribd.com/document/811452484/OXI-One-User-Manual)).
The spreadsheet has separate tabs for the One MK1, the One MKII and the E16.

| Item | Bytes / format |
|------|----------------|
| SysEx header | `F0 00 21 5B <model> 01`. Model: `00` = One MK1, `01` = One MKII (`02` = E16) |
| Enter remote mode | `06 55` |
| Acknowledge (Oxi → host) | `06 53` |
| Exit remote mode | `00` |
| Grid LED | `01 <id> <r> <g> <b>` |
| Button LED | `02 <led_id> <value>`. Value: 0 off, 1 on, 2 blink, 4 fast blink, 6 short on |
| OLED dump | `03` + 1024 bytes, sent as 2048 nibbles (128×64 pixels, 1 bit each) |
| Pads (Oxi → host) | note on/off, **channel 1**, note = `x + y·16` (16×8 grid) |
| Function buttons (Oxi → host) | note on/off, **channel 2**, note = `FunctionButtons_e` index |
| Encoders (Oxi → host) | CC, **channel 1**, CC number = encoder from the left. Value `01–08` = clockwise steps, `11–18` = counter-clockwise steps (accelerated) |

* **Nibble encoding.** Every payload byte is sent as two 7-bit-safe nibbles,
  high nibble first. This is checked against both examples in the sheet. For
  example, `01 01 03 00 01 00 01 00 01` = pad `0x13`, colour `01/01/01`.
* The `06` category byte is only needed for the enter command ("no longer
  necessary once in REMOTE MODE").
* The MK1 and MKII share most of the button enums, but the MKII adds SEQ 5–8
  and RGB sequencer LEDs (`LEDS_seq_1` = 58 … `LEDS_seq_8_sel` = 73). Both
  enum tables are copied into `src/oxi_protocol.h`.
* **Unclear or not documented, so test on hardware (milestone M0):**
  * The sheet says "x [0:7], y [0:15]" but also `id = x + y·16`. Assumed: 16
    columns × 8 rows.
  * The OLED pixel packing. Assumed: SSD1306 page layout.
  * Whether encoder CC numbers start at 0 or 1.
  * Which USB cable the remote messages use.
  * Whether the Oxi's own sequencer and its MIDI output keep running in
    remote mode.
  * Whether the ACK always arrives.
  * The oxi-mcp author also reports that the ACK, event decoding and OLED
    packing are unverified on hardware.

## M8 side

The M8 has no official MIDI implementation chart. Sources: the community
write-up [M8 basic MIDI](https://handeyeco.github.io/tech-blog/m8-basic-midi)
(reverse-engineered from the OMX-27 code) and the
[M8 manual](https://cdn.shopify.com/s/files/1/0455/0485/6229/files/m8_operation_manual_v20230630.pdf)
(MIDI Settings and MIDI Mapping views).

| M8 function | MIDI (on the M8's *Control Map channel*, default 10) |
|-------------|------------------------------------------------------|
| Play, Shift, Edit, Option | notes 0, 1, 2, 3 (on = press, off = release) |
| Left, Right, Up, Down | notes 4, 5, 6, 7 |
| Mute track 1–8 | notes 12–19 |
| Solo track 1–8 | notes 20–27 (note-off un-solos) |
| Any parameter | CCs, assigned with MIDI learn in the M8's MIDI Mapping view |
| Play instruments | notes on the track's MIDI input channel (M8 MIDI settings) |

* **Connection.** The Oxi takes OxiMate's only USB host port, so the M8
  connects over **TRS or DIN MIDI**: OxiMate's DIN/TRS OUT goes to the M8's
  MIDI IN. The Unit MIDI has TRS jacks; check the M8 model and the TRS type
  (A or B).
* The M8 doesn't report its mute/solo state back, so the Oxi LEDs show what
  OxiMate last sent. Muting on the M8 itself makes them drift. A **Sync**
  action re-sends OxiMate's state.

## Control layout (first version)

Oxi grid (16 columns × 8 rows):

```
 col  0 1 2 3 4 5 6 7 8 9 A B C D E F
 y0   ┐
 ..   ├ jam keyboard: isomorphic, each row a 4th up,
 y5   ┘ in-scale pads lit, roots in white
 y6   T1 .. T8 jam-track select │ oct- oct+ scale- scale+ . . . .
 y7   mute 1..8 (green/red)     │ solo 1..8 (off/yellow)
```

| Oxi control | Action |
|-------------|--------|
| Play | M8 Play (start/stop) |
| Shift | M8 Shift (held) |
| Arrows (left_16, up_32, down_48, right_64) | M8 Left / Up / Down / Right (held, so key-repeat works) |
| Encoder 1–4 buttons | M8 Edit, Option, Shift+Edit (hold Shift, then press and release Edit), *Sync* (re-send mutes/solos) |
| Encoders 1–4 | CC on the CC channel, relative turns converted to absolute 0–127 |
| page_button (MKII) / mod_button (MK1) | next encoder bank (4 banks × 4 encoders = 16 CCs) |
| Remaining buttons | unused at first; listed in the settings table for later |

* **Jam notes** go to the M8 track's input channel (`jamBaseChannel` + track
  − 1). Each pad lights while held.
* **Encoder CCs**: the default is CC 20–35 on channel 16. The user maps them
  in the M8 with MIDI learn.
* **OLED**: shows the mode, the 4 encoder names and values of the current
  bank, and the jam track, octave and scale. It is drawn on a 1-bit 128×64
  `M5Canvas`, converted to the Oxi's packing, and sent only when it changes
  (≤ 10 fps).
* **Feedback from the M8 clock**: if the M8 sends clock to DIN IN,
  `ClockTracker` provides the BPM and run state. The Oxi Play LED blinks on
  the beat while the M8 plays.

## Core2 page

```
┌────────────────────────────────────────┐
│Oxi>     USB 2E8A:1234   DIN 120.0 PLAY │
│Oxi One MKII   remote: ON  (ack ok)     │
│M8 ctrl ch 10   jam T3 ch 3  C min  O4  │
│Bank 1  CC20  CC21  CC22  CC23          │
│        [ 64] [ 12] [127] [  0]         │
│Mute  1 2 3 4 5 6 7 8                   │
│      . M . . . S . .                   │
│[Ctrl ch 10][Jam ch 1][CC 20][Layout 1] │  tap to cycle a setting
│Last: pad 3,5 on -> M8 E4 ch3           │
│  Remote:on      Bank+        Sync      │  touch buttons A / B / C
└────────────────────────────────────────┘
```

* **A**: turn remote mode on or off. Turning it on enters remote mode now and
  after every reconnect; turning it off sends Exit.
* **B**: next encoder bank (same as the Oxi page button).
* **C**: Sync. Re-sends mute/solo state to the M8 and redraws all Oxi LEDs
  and the OLED.
* Remote mode keeps working whichever OxiMate page is shown.

## LED bar

While remote mode is active, the remote layer takes over the LED bar:

* LEDs 0–7 show M8 tracks 1–8: dim green = playing, red = muted, yellow =
  soloed.
* LEDs 8–9 show remote status: blue = remote OK; red blinking = no
  acknowledgement or Oxi disconnected.
* This needs the LED override API that later modes share. Add it to
  `src/leds.*` here:
  * `setPixel(index, colour)` sets one LED.
  * `setOwner(LedOwner)` hands the bar to a mode; activity flashes are the
    default owner.

## Code

| File | Purpose |
|------|---------|
| `src/oxi_protocol.h` | Header and model bytes, command IDs, button and LED enums for MK1 and MKII, nibble encode/decode. Pure functions, host-testable |
| `src/oxi_remote.*` | Device layer: detect model, enter and exit remote mode, ACK timeout and retry, shadow state for grid and button LEDs (sends only what changed), OLED framebuffer upload, decodes incoming notes and CCs into `onPad(x, y, down)`, `onButton(id, down)` and `onEncoder(n, delta)` |
| `src/oxi_m8.*` | M8 surface: layout, jam keyboard and scales, mutes and solos, encoder banks, M8 key notes; builds outgoing messages |
| `src/oxi_page.*` | The Core2 Oxi page (`Page` subclass) |

**How it fits in:**

* **Consuming events.** In `onMidi()` the remote layer gets the first look:
  `if (oxiRemote.consume(msg)) { monitor.log(msg, Handled); return; }`.
  It only consumes USB messages while remote mode is active: channel 1–2
  notes and CCs, plus its own SysEx (ACK). Everything else goes to
  `router.route()` as before. The Monitor shows consumed messages in a new
  "handled" colour (green), not grey.
* **Generated output.** M8 commands are new messages from OxiMate itself, not
  from an input. Plan: add `MidiSource::Internal` and `Router::sendInternal()`,
  which sends to DIN OUT and skips the input filters.
  * Arrays indexed by `MidiSource` grow to `kSourceCount`: `ClockTracker`,
    `RouterConfig::channelMask` and `forceChannel` (bump
    `RouterConfig::kVersion`), and `Leds::_flash`.
  * Stage 8 (`src/clock_engine.*`) also needs an internal source, so agree
    this API with stage 8. If the stage 8 MIDI timing task already exists,
    `sendInternal()` hands messages to that task's output queue instead.
* **Sending to the Oxi**: `UsbMidi::send()` with SysEx messages, on the cable
  the ACK arrived on. `USBH_MIDI::SendSysEx` splits large messages into USB
  packets, so the 2 KB OLED dump needs no change. `kMaxSysExLength` only
  limits *received* SysEx.
* **Connect detection**: `UsbMidi` gets an `onConnect` callback, wrapping
  `USBH_MIDI::attachOnInit`. When remote mode is enabled, OxiMate sends Enter
  with the MKII header, then the MK1 header. The ACK's model byte identifies
  the device. No ACK within 500 ms: show "no ACK" and retry every 2 s, up to 3
  times.
* **Held notes**: jam notes and held M8 keys are released on disconnect,
  remote-off or a page change of the jam track. Use `src/note_tracker.*`
  (stage 16) once it exists; until then, keep a local held-note set.

## Settings (NVS key `oxi`, versioned struct)

`enabled`, `m8ControlChannel` (10), `jamBaseChannel` (1), `ccChannel` (16),
`ccBase` (20), `layout` (1), `scale` and `root`, `octave`. Saved with a 2 s
delay, the same as `RouterConfig`. Encoder values are not saved.

## Milestones

1. **M0, verify the protocol** using the current firmware plus a temporary
   "Send Enter" button on the Monitor page:
   * Log the ACK, pad, button and encoder events, and which USB cable they use.
   * Check whether the Oxi's own sequencer keeps running and sending MIDI.
   * Record the results in this section.
2. **M1, `oxi_protocol.h` + host tests**: nibble encode/decode, building
   messages, parsing events, decoding relative encoder values.
3. **M2, `oxi_remote.*`**:
   * enter, exit and reconnect, with the ACK timeout
   * LED shadow state and diffing
   * a test pattern that checks the grid orientation and all button LED IDs
4. **M3, `oxi_m8.*`**:
   * transport and cursor keys, mutes and solos, encoder banks to CCs, jam
     keyboard with scales
   * `MidiSource::Internal` and `Router::sendInternal()`
5. **M4, OLED**: check the pixel packing with test patterns (one pixel at the
   top left, a checkerboard), then the status screen.
6. **M5, Oxi page, NVS settings and LED bar.** Update the README and mark the
   stage implemented.

## Test plan

* **Host (as in stages 1–2)**:
  * nibble round trip
  * the two example messages from the sheet, byte for byte
  * pad ID → (x, y) decoding
  * encoder value → signed delta (`0x03` → +3, `0x13` → −3)
  * jam layout → note numbers
  * M8 note table
  * consumed and passed-through messages in `onMidi()`
* **Hardware**:
  * ACK received for MK1 and/or MKII
  * every pad lights at the expected position
  * all button LED IDs walked through
  * OLED test patterns
  * Oxi unplugged and plugged back in: remote mode returns and held notes
    are released
  * how the Oxi behaves on OxiMate's USB power
* **M8**:
  * verify the Control Map channel
  * notes 0–7 move the cursor, and Shift + arrows work
  * mute/solo notes 12–27
  * a CC learned in the MIDI Mapping view follows encoder 1
  * jam notes play the selected track's instrument

## Risks

* **The protocol is unverified on hardware.** The sheet contains
  contradictions (grid axes, "BUTTON_ID 1–29" next to a 55-entry enum) and
  "TBD" rows. M0 comes first for this reason.
* **One USB host port.** The Oxi uses it, so the M8 must use TRS/DIN MIDI. A
  USB hub would need several `USBH_MIDI` instances and hub support, which is a
  later extension.
* **Power.** The USB host module may not be able to power the Oxi. Check
  whether the Oxi needs its own power supply.
* **The M8 note map is community-sourced** and could change with firmware.
  Keep all M8 note and channel numbers in one table in `oxi_m8.cpp`.
* **State drift**: the M8 doesn't report mutes or solos back (see the Sync
  action).
* **Bandwidth**: a full grid redraw is about 128 × 14 bytes ≈ 1.8 KB, plus
  2 KB per OLED frame, over the MAX3421E, which shares the SPI bus with the
  LCD. Diffing and the 10 fps OLED limit keep this low. Stage 8's MIDI task
  will own USB polling.

## Open questions

1. Is your Oxi One a **MK1 or MKII**, and which firmware version is it on?
   The remote spec appears from v4.5 onwards.
2. Which **M8 model** do you have, and how will it connect: TRS MIDI IN (type
   A or B?) or USB? What is its *Control Map channel* set to?
3. Layout priority: is the jam keyboard plus mutes/solos right, or do you want
   more grid space for something else (e.g. M8 chain launching, as with the
   Launchpad Pro)?
4. In remote mode, should the Oxi's **own sequencer** still play (if the
   firmware allows), or is the Oxi purely a controller while OxiMate controls
   it?
