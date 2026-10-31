# Stage 4: Push 2 host

> **Status: plan, not implemented.** Written 2026-10-31. Where this plan and the README's
> [Shared infrastructure](../../README.md#shared-infrastructure) section disagree, the README wins:
>
> * `MidiSource::Internal` / `Router::sendInternal()` → use the shared `MidiOutputs::send()` with an `Internal` origin.
> * Push traffic handling in `onMidi()` → register as an input interceptor.
> * LED bar → use the shared LED claim API.

Use an Ableton Push 2 without a computer, with OxiMate as its USB host. The
Push 2 has no standalone mode. It works in two modes:

* **Sequencer mode**: an 8-track step sequencer, comparable to the Oxi One.
  Notes go out over DIN (to the M8, the SAM2695 and other gear).
* **Play mode**: a control surface. The pads work as a keyboard, the encoders
  send CCs, the touch strip sends pitch bend or modulation, and the transport
  buttons start and stop the clock.

Main reference: Ableton's
[Push 2 MIDI and Display Interface Manual](https://github.com/Ableton/push-interface/blob/master/doc/AbletonPush2MIDIDisplayInterface.asc)
(called "the manual" below). Items marked **(verify)** have not been
confirmed and need checking on the hardware with the Monitor page.

## Feasibility gate: USB full speed (do this first)

* The MAX3421E is a **full-speed** (12 Mbit/s) host. The Push 2 is a USB 2.0
  device, and the manual doesn't say whether it works at full speed. I found
  no report either way.
* The USB 2.0 spec requires high-speed devices to enumerate at full speed.
  Whether everything works at full speed is a separate question **(verify)**.
* **Milestone 0 decides whether stage 4 goes ahead.** If the Push 2 doesn't
  work at full speed, the fallback is a different host with a high-speed USB
  host port (e.g. a Teensy 4.1), which is outside this project. A USB hub
  doesn't help, because the host itself is still full speed.

## Hardware and power

* Connect the Push 2 to the USB Host Module, **with its own power supply
  connected**. The Core2 battery and its 5 V output can't supply a 500 mA bus
  device.
* Without the power supply, the Push limits itself: global LED brightness to
  8 of 127, and display backlight to 100 (about 7%) (manual, "Brightness").
* Request Statistics (SysEx `0x1A`) reports the power source (1 = external
  supply). OxiMate reads it at connect and sets the brightness to match.

## USB facts

| Item | Value | Source |
|------|-------|--------|
| VID:PID | `2982:1967` | manual |
| Interfaces | MIDI streaming, plus a vendor bulk interface (0) for the display | manual |
| Display endpoint | bulk OUT `0x01` | manual |
| MIDI ports | Live port and User port, as two USB-MIDI cables | manual (port names) |
| Cable numbers | Live = cable 0, User = cable 1 | **(verify)**, inferred from Linux port names `nn:0`/`nn:1` |
| Default mode | Live; set with SysEx `F0 00 21 1D 01 01 0A <0 Live/1 User/2 Dual> F7` | manual |

* **`USBH_MIDI` already fits the Push.** At startup it only accepts
  interfaces of class Audio / MIDI Streaming
  (`libraries/USB_Host_Shield_2.0/usbh_midi.cpp`, `MidiDescParser`), so it
  takes the MIDI endpoints and ignores the display interface. It caps
  packets at 64 bytes, which is the full-speed bulk maximum anyway.
* **OxiMate uses the Live port (cable 0)** and stays in the default Live
  mode. With no Ableton Live running, the Live port is the one meant for a
  host, so no mode switch is needed. Our `UsbMidi` already keeps cable
  numbers. The Monitor shows the User port as `USB2`.

## Push 2 MIDI map (manual)

| Control | Message |
|---------|---------|
| Pads 8×8 | notes 36–99, bottom-left = 36, bottom-right = 43, top-right = 99. Row r (0 = bottom), column c: note = 36 + 8r + c |
| Pad aftertouch | channel pressure by default; poly pressure via SysEx `1E 01` |
| Track encoders 1–8 | CC 71–78, relative (1–63 = clockwise, 65–127 = counter-clockwise, two's complement) |
| Master / Tempo / Swing encoders | CC 79 / 14 / 15 |
| Encoder touch | note-on 127 / 0 (note numbers **(verify)**) |
| Touch strip | pitch bend (14-bit, centre 8192); touch = note 12 |
| Buttons | CC, 127 = pressed, 0 = released. Play 85, Record 86, Shift 49, Select 48, Note 50, Session 51, Octave ↓/↑ 54/55, Page ◀/▶ 62/63, Arrows 44–47, Mute 60, Solo 61, Scale 58, Delete 118, Duplicate 88, Undo 119, Tap tempo 3, Metronome 9, Scenes 36–43 (Scene 8 → Scene 1), track buttons above the display 102–109, below the display 20–27 |

**LEDs.** Send a note-on (pads) or a CC (buttons) back to the Push:

* The velocity is the palette index, and the **MIDI channel selects the
  animation**: 0 = static, 1–5 = one-shot, 6–10 = pulse, 11–15 = blink. The
  animations stay in sync with MIDI clock sent to the Push.
* Default palette entries: 0 black, 122 white, 123 light grey, 124 dark grey,
  125 blue, 126 green, 127 red.
* Set a palette entry with SysEx `0x03`, then make it take effect with
  "reapply palette" (`0x05`).

## Sequencer design (shared with stage 5)

* **8 tracks.** Each track has:
  * a mode: **Drum** (one fixed note) or **Melodic** (a note per step)
  * a MIDI channel and an output (DIN OUT, which also feeds the SAM2695)
  * a length (1–64 steps) and a clock division (1/32, 1/16, 1/8 or 1/4)
  * mute, and transpose
* **Per step:** on/off, note, velocity, length in clock ticks (24 PPQN), and
  probability (0–100%).
* **Patterns:** 16 patterns, selected with the Scene buttons. Shift + Scene
  queues the next pattern, which starts when the current one reaches its end.
* **Memory:** 5 bytes per step × 64 steps × 8 tracks ≈ 2.6 KB per pattern, so
  16 patterns ≈ 42 KB in RAM.
* **Timing:** the sequencer runs from stage 8's `ClockEngine` (24 PPQN
  ticks, transport and swing, inside the MIDI timing task). It schedules its
  own note-offs and releases every sounding note on Stop.

**Pad layout in sequencer mode**

```
 Pad rows (top → bottom)
 ┌───────────────────────────────┐
 │ 1  2  3  4  5  6  7  8        │ steps 1–8    ┐
 │ 9  10 11 12 13 14 15 16       │ steps 9–16   │ 32 steps of the
 │ 17 ...                 24     │ steps 17–24  │ selected track
 │ 25 ...                 32     │ steps 25–32  ┘ (Page ◀▶ for 33–64)
 │ T1 T2 T3 T4 T5 T6 T7 T8       │ track select + audition
 │ ┌ Drum: 16 velocity levels ┐  │
 │ │ Melodic: 3-row in-scale  │  │ bottom 3 rows
 │ └ keyboard (set note)      ┘  │
 └───────────────────────────────┘
```

* **Tap a step** to toggle it. **Hold a step** and turn encoders 1–4 to set
  note, velocity, length and probability. Encoder 8 sets the track's MIDI
  channel.
* Track buttons above the display (CC 102–109) select tracks. The buttons
  below the display (CC 20–27) mute tracks; lit means the track is playing.
* **Play** starts or stops the `ClockEngine`. **Record** turns on step
  recording: pressed pads or keys go into the steps.
* **Delete + step** clears a step. **Duplicate** copies the current page to
  the next page. The **Tempo/Swing** encoders and **Tap tempo** control the
  `ClockEngine`.
* **Session** switches to sequencer mode, **Note** to play mode.

**Pad colours.** Track colours are palette entries 1–8 (set at connect).
Lower-probability steps use dimmed versions in entries 9–16.

| Pad state | Colour |
|-----------|--------|
| Step on | track colour |
| Step on, probability under 100% | dimmed track colour |
| Step off | dark grey (124) |
| Playhead | white (122) |
| Track pad (selected) | track colour, pulsing (channel 8) |

## Play mode (control surface)

* **Pads:** an in-key isomorphic keyboard in fourths, with the root note
  highlighted. Notes go to the selected track's channel and output.
  Aftertouch is sent as channel pressure.
* **Encoders 1–8:** send CCs. The encoders are relative, so OxiMate keeps the
  current value of each CC and converts turns into absolute values (0–127).
  The default CCs are 71–78, editable on the Core2 page.
* **Touch strip:** pitch bend, or the mod wheel (CC 1), selectable.
* **Transport:** Play and Stop go through the `ClockEngine`, so DIN gets
  Start/Stop and clock.
* **M8:** the M8 plays notes on the channels it listens on. Map CCs to M8
  parameters on the M8 itself **(verify against the M8 manual)**. OxiMate
  only sends generic CCs.

## Core2 page: "Push2"

The Push display isn't used (see Display below), so the Core2 screen shows
what the Push 2 would normally show.

```
┌────────────────────────────────────────┐
│Push2>   USB 2982:1967   INT 120.0 PLAY │
│SEQ  Pattern 3 (next 4)  Page 1/2       │
│T1 Kick  ch10 ■□□□■□□□■□□□■□□□          │
│T2 Snare ch10 □□□□■□□□□□□□■□□□          │
│T3 Hat   ch10 ■■■■■■■■■■■■■■■■          │
│T4 Bass  ch 2 ■□□■□□■□□□■□■□□□  <       │
│T5 --    ch 3 □□□□□□□□□□□□□□□□          │
│  ...T6–T8                              │
│Step 4: C2 v100 len 1/16 p 75%          │
│Enc: Note Vel Len Prob . . . Chan       │
│   Play        Mode:Seq      Save       │
└────────────────────────────────────────┘
```

* **Buttons:** **A** Play/Stop, **B** switches mode (Seq ⇄ Play), **C** saves
  the patterns to SD. Tap a track row to select that track. Tap the header to
  switch pages, as on every page.
* Without a Push 2 connected, the page shows "Connect Push 2 (with PSU)" and
  the setup tips.

## LED bar

* **Left strip:** the beat within the bar (LEDs 1–4), with the downbeat in a
  different colour. LED 5 is red while step recording.
* **Right strip:** the 16th note within the beat (LEDs 1–4). LED 5 shows
  transport (green = playing).
* The Push page takes the LED bar over only while it's shown. Otherwise the
  LEDs show input activity (stage 2).

## Display strategy

* **Default: the Push display stays dark.** The Core2 screen shows status
  instead.
* **Optional milestone (experimental):** a low-fps Push display. The rough
  numbers suggest it isn't worth it:
  * One frame is 16 header bytes plus 327,680 bytes of pixel data. That's
    960×160 pixels in BGR565, with each line padded to 2048 bytes and XORed
    with `E7 F3 E7 FF` (manual).
  * At full speed that's 5,120 bulk packets of 64 bytes per frame. The
    MAX3421E over SPI probably manages about 300–600 KB/s (**estimate**), so
    one frame takes about 0.5–1 s.
  * The Push blanks the display if no frame arrives for 2 s, so OxiMate
    would have to resend continuously. That ties up the SPI bus, which is
    shared with the Core2 LCD and SD card, and hurts MIDI latency.
  * It also needs a frame buffer in PSRAM and a small driver that claims bulk
    endpoint `0x01` on interface 0. `USBH_MIDI` only has entries for the
    control and two MIDI endpoints.
  * If attempted: show a static status screen only while the transport is
    stopped.

## Code plan

| File | Purpose |
|------|---------|
| `src/push2.h/.cpp` | `Push2Host`: recognises the Push by VID:PID, sends the setup SysEx (palette, brightness), handles input, renders the LEDs (only changed LEDs are sent, in batches), switches between sequencer and play mode |
| `src/push2_page.h/.cpp` | the Core2 "Push2" page (a `Page`) |
| `src/step_sequencer.h/.cpp` | the sequencer core, independent of the Push; stage 5 reuses it |
| `src/sequencer_store.h/.cpp` | saving patterns to SD (binary file, with a version byte) |

**`StepSequencer` interface (shared with stage 5)**

```cpp
struct Step {            // 5 bytes
  uint8_t note;          // melodic note (drum tracks use Track::drumNote)
  uint8_t velocity;      // 1–127
  uint8_t length;        // gate length in ticks (24 PPQN; 6 = 1/16)
  uint8_t probability;   // 0–100
  uint8_t flags;         // bit 0 on, bit 1 accent, bit 2 tie
};
struct Track {
  uint8_t mode;          // Drum or Melodic
  uint8_t channel;       // 1–16
  uint8_t drumNote;
  uint8_t length;        // 1–64 steps
  uint8_t division;      // ticks per step: 3, 6, 12, 24
  int8_t transpose;
  bool muted;
  Step steps[64];
};
struct Pattern { Track tracks[8]; };

class StepSequencer {
public:
  // Called from the MIDI timing task; the caller turns notes into MidiMessages
  typedef void (*NoteOut)(uint8_t track, uint8_t channel, uint8_t note,
                          uint8_t velocity, bool on, void *ctx);
  void begin(NoteOut out, void *ctx);
  void onTick(uint32_t tick);          // from ClockEngine, 24 PPQN, swing already applied
  void onStart(); void onStop();       // onStop releases all sounding notes
  void onContinue(uint32_t tick);      // song position support
  Pattern &pattern(int index);
  void selectPattern(int index, bool atEnd); // atEnd: queued switch
  uint8_t playhead(int track) const;   // for LEDs / UI
  // Edits come from the UI core: short critical sections (portMUX)
  void toggleStep(int track, int step);
  void setStep(int track, int step, const Step &s);
};
```

**What stage 4 needs from other modules:**

* **`ClockEngine` (stage 8):** tick and transport callbacks in the MIDI
  timing task, with swing already applied.
* **`NoteTracker` (stage 16):** sequencer notes are registered there so
  panic and stop can release them.
* **`UsbMidi`:**
  * `sendPackets(const uint8_t *events, n)`, which sends up to 16 USB-MIDI
    events per 64-byte transfer using `SendRawData`. The current
    `SendData` sends one message per transfer, so a full redraw of the 64
    pads would take 64 transfers.
  * Thread-safe sending once stage 8 moves USB polling into the timing task.

**How it plugs into the existing code:**

* `onMidi()`: when `push2.connected()`, USB messages go to `push2.handle()`
  first and are **not** sent to the router. Without this, Push pad and
  button traffic would go out to DIN. The Monitor shows Push messages in
  grey.
* Add a new **`MidiSource::Internal`** for messages the sequencer and play
  mode generate. Add `Router::sendInternal(msg)`, which sends to DIN OUT
  only and skips the input filters. `RouterConfig::kVersion` goes to 2.
* While a Push 2 is connected, the router turns off **DIN→USB**. DIN traffic
  would otherwise reach the Push as LED commands.
* Register the page with `ui.addPage(push2Page)`. The LED renderer runs in
  the UI loop at up to 50 Hz and only sends changed LEDs.

## Saved data

| What | Where |
|------|-------|
| Patterns (16 × ~2.6 KB) | SD card: `/oximate/push2/patterns.bin` (version byte, CRC) |
| Push settings: default mode, scale/root, encoder CC map, touch-strip function, last pattern | NVS key `push2` |
| Quick save of the current pattern without an SD card | NVS (one pattern fits) |

## Milestones

0. **Feasibility.** Flash the stage 2 firmware and connect the Push 2 with
   its power supply. Check that the header shows `2982:1967` and that pads
   and buttons appear in the Monitor. Find out which cable carries what, and
   the encoder touch note numbers. **Decide whether stage 4 goes ahead.**
1. **Driver.** `Push2Host` recognises the Push, runs setup, takes its input
   away from the router, and renders LEDs (changed only, batched). Test:
   pressing a pad lights it.
2. **Play mode.** Keyboard layout, encoders to CCs, touch strip, transport,
   and the Push2 page.
3. **Sequencer core.** `StepSequencer` with Mac unit tests (as for the
   parsers), driven by `ClockEngine`. Needs stage 8's clock engine first.
4. **Sequencer on the Push.** Step view, track select and mute, pages, step
   editing with the encoders, pattern select and queueing, LED bar.
5. **Saving and recording.** SD/NVS storage, step recording, Duplicate and
   Delete, pattern chaining.
6. *(optional)* Push display experiment.
7. *(optional)* A Push 2 and another USB MIDI device at the same time,
   through a powered hub (`USBHub` plus a second `USBH_MIDI`;
   `MidiMessage` would need a device index).

## Test plan

* **Unit tests on the Mac** (`StepSequencer`):
  * step timing at each division and track length
  * note-offs scheduled for the step length, and all notes released on Stop
  * probability with a seeded random generator
  * a queued pattern switching exactly at the pattern end
  * continue from a song position
* **LED renderer:** a diff test (only changed LEDs produce messages).
* **On hardware:**
  * enumeration with and without the power supply (milestone 0)
  * a full redraw of the 64 pads in under 20 ms
  * pad-to-DIN latency (Monitor timestamps or a scope)
  * 8 tracks of 1/16 notes at 300 BPM. DIN load is about 1 KB/s, a third of
    its capacity.
  * the M8 receiving notes and clock on the right channels
  * hot-unplugging the Push: notes released, router back to normal

## Risks

* **Full speed (biggest risk):** if the Push 2 doesn't work at full speed,
  stage 4 needs different hardware.
* **Power:** without its power supply the Push is too dim to use.
* **USB load:** aftertouch and encoders can produce a lot of USB traffic. The
  Push host filters what it doesn't use, and the Monitor greys out Push
  traffic.
* **Threading:** the USB host isn't thread-safe. Once stage 8 moves USB into
  the timing task, every Push LED write has to go through a queue.
* **Push display:** probably not practical; it stays optional.

## Open questions

1. Do you have the Push 2 power supply?
2. Should every track be switchable between drum and melodic mode, or should
   tracks 1–4 be fixed as drums and 5–8 as melodic?
3. Which outputs and channels should sequencer tracks default to? For
   example, M8 tracks 1–8 on channels 1–8 over DIN.
4. Should the Push 2 work together with another USB MIDI device through a
   hub, or is it alone on the USB port?
5. Is there an SD card in the Core2 for pattern storage?
6. Which M8 parameters should the encoders control by default in play mode?
