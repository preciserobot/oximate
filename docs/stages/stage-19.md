# Stage 19: Extra controls

> **Status: plan, not implemented.** Written 2026-11-01. Where this plan and the README's
> [Shared infrastructure](../../README.md#shared-infrastructure) section disagree, the README wins.

Physical knobs, buttons and faders from M5Stack Grove units, plugged into the
free Port A (red, I2C) and Port B (black, GPIO/ADC). Port C stays with the
Unit MIDI. They can:

* **drive the visible page** (e.g. the Synth page's layer volumes, the Clock
  page's tempo and swing, the drum machine's lanes), or
* **send MIDI CC** with learnable mappings, from a new **Controls** page, so
  OxiMate becomes a small controller for the M8, the Elektrons or the DFAM
  (stage 20).

## Units that fit

| Unit | Port | Interface | What it has | Source |
|------|------|-----------|-------------|--------|
| **Unit 8Encoder** (recommended) | A | I2C `0x41`, 5 V, STM32F030 | 8 endless encoders with push, 1 toggle switch, 9 RGB LEDs (WS2812C) | [M5Stack docs](https://docs.m5stack.com/en/unit/Unit_8Encoder) |
| **Unit 8Angle** | A | I2C `0x43`, STM32F030 | 8 potentiometers, 1 toggle switch, 9 RGB LEDs (SK6812) | [M5Stack docs](https://docs.m5stack.com/en/unit/8Angle), [ESPHome driver](https://esphome.io/components/sensor/m5stack_8angle.html) |
| **Unit Joystick2** | A | I2C `0x63`, STM32G030 | X/Y (16-bit), push, 1 RGB LED. The older Joystick unit is `0x52`, 8-bit | [Switch Science](https://www.switch-science.com/products/9922) |
| **Unit Fader** | B | analog (white wire) + RGB data (yellow wire) | 1 slide potentiometer, 14 SK6812 LEDs | [M5Stack docs](https://docs.m5stack.com/en/unit/fader) |
| **Unit Dual Button** | B | 2 GPIO inputs | 2 buttons | (not checked; two plain GPIO lines on Port B) |

* **Libraries:** M5Stack's [M5Unit-8Encoder](https://github.com/m5stack/M5Unit-8Encoder)
  and Rob Tillaart's [M5ROTATE8](https://github.com/RobTillaart/M5ROTATE8),
  which documents absolute and relative counters per encoder, key state, the
  toggle switch, 9 RGB LEDs, and that 400 kHz works (600 kHz doesn't).
  OxiMate will talk to the units with M5Unified's `I2C_Class` register calls
  (`readRegister`/`writeRegister`) rather than add a library per unit; the
  register numbers come from those libraries and are checked in milestone 1.
* **Encoders vs. potentiometers:** encoders (8Encoder) never jump, because
  they only send changes, and their LEDs can show the current value. Pots
  (8Angle) are absolute and need "pickup" (see below). For live use with
  changing pages, the **8Encoder is the better first unit**.

## Buses

* **Port A is its own I2C bus.** In M5Unified, the Core2's internal bus
  (`In_I2C`: touch, power chip, IMU, RTC) is on GPIO 21/22 using I2C
  controller 1, and Port A (`Ex_I2C`) is on GPIO 32/33 using controller 0
  (checked in M5Unified's `M5Unified.cpp`: the pin table entry for Core2 and
  the port assignment in `_begin`). So polling Port A never waits for touch or
  the vibration motor.
* M5Unified's `I2C_Class` has **no locking**, so every access to `Ex_I2C`
  must come from one task.
* **Stage 20's DFAM CV board** (an MCP4728 DAC at `0x60`–`0x67`, or M5Stack's
  DAC 2 unit) also goes on Port A. Keep the MCP4728 at `0x60`; `0x63` would
  clash with Joystick2. Both stages use the same I/O task (below).
* **Port B** (GPIO 26 output/DAC, GPIO 36 input-only/ADC) suits the Fader
  (ADC on 36, LED data on 26) or the Dual Button. GPIO 26 was also listed in
  the stage 20 notes as the only free built-in DAC pin; a unit on Port B takes
  it.
* **5 V:** Grove units get 5 V from the Core2. When running on battery, the
  Core2's 5 V output must be on (`M5.Power.setExtOutput(true)`); M5Unified
  turns it on by default, and the Unit MIDI already relies on it.

## Where the polling runs

* **Not in the MIDI task:** an I2C read of 8 encoders takes about 1 ms at
  400 kHz, and the MIDI task must stay free for timing.
* **Not in `loop()`:** the UI loop can take ~70 ms during a full redraw, which
  would make knobs feel laggy and CC output jittery.
* **A new I/O task** (`src/io_task.*`) on core 1, priority 3 (above
  `loopTask`, below nothing that matters on that core):
  * owns `Ex_I2C` and Port B;
  * polls the 8Encoder every 5 ms (relative counters, keys, switch: about
    40 bytes), the 8Angle every 10 ms, the fader ADC every 5 ms;
  * writes unit LEDs only when their colour changed;
  * scans for units every 2 s, so they can be plugged in while running;
  * later also writes the DFAM CV board (stage 20).
* **Events out of the I/O task:**
  * CC mappings go straight to `MidiTask::send()` (the existing UI-to-MIDI
    queue), so CC output never waits for a screen redraw;
  * page actions go to a small queue that `loop()` drains and passes to the
    visible page.

## Page control

A new optional `Page` API, used by pages that want physical controls:

```cpp
virtual int controlCount() const { return 0; }              // how many encoders the page uses
virtual const char *controlLabel(int i) const { return ""; }
virtual void onControl(int i, int delta) {}                 // encoder turned (relative)
virtual void onControlPress(int i) {}
virtual RGBColor controlColor(int i) const { return {}; }   // LED colour for encoder i
```

Examples:

| Page | Encoders 1–8 |
|------|--------------|
| Synth | layer 1–4 volume, drum volume, master, reverb, chorus |
| Clock | tempo (push: ×10 steps), swing, ratio DIN, ratio USB, … |
| Drums | lane 1–8 velocity (push: mute) |
| Router | — (touch is enough) |

* While a page uses the encoders, the header shows a small knob icon and
  touching an encoder (any movement) briefly shows its label and value in the
  header (the same flash used for PANIC).
* Encoder speed: faster turning gives bigger steps (acceleration).

## MIDI mode and the Controls page

The 8Encoder's **toggle switch** selects **Page** (switch off) or **MIDI**
(switch on), so the same knobs work both ways. The Controls page edits the
MIDI mappings:

```
┌────────────────────────────────────────┐
│Controls> USB ok INT 120.0 STOP  ▮▮ 87% │
│ 8Encoder @41  mode MIDI (switch)       │
│ #  Out  Ch  CC   Min Max  Value        │
│>1  DIN  1   74    0 127    64          │  selected: tap = learn
│ 2  DIN  1   71    0 127    20          │
│ 3  USB  1   10    0 127    64          │
│ 4  DIN  2   74    0 127   100          │
│ ...                                    │
│ [Learn] [Relative off] [Pickup on]     │
│    Prev         Learn         Next     │  touch buttons A / B / C
└────────────────────────────────────────┘
```

* **Per control:** output (DIN, USB, both), channel, CC number, min/max,
  and for push buttons: off, CC toggle (0/127), CC momentary or a note.
* **Learn:** select a row, tap Learn, then send a CC from your gear into
  OxiMate (e.g. move a knob on the Elektron); the channel and CC are taken
  from it. Learning reads the next incoming CC with an input hook.
* **Values are kept per control**, so encoders continue from the last sent
  value; the LED ring colour shows the value (dark → bright).
* **Pots (8Angle) and the fader** send absolute values. **Pickup** (on by
  default) waits until the pot passes the last sent value before sending, so
  turning a pot after a page or preset change doesn't make the value jump.
* **Output rate:** at most one CC per control every 5 ms, and only when the
  value changed, so a fast turn can't flood DIN (3 KB/s).

## LEDs

* **8Encoder:** in Page mode each LED shows the page's `controlColor()`; in
  MIDI mode it shows the value; the 9th LED (by the switch) shows the mode
  (Page = blue, MIDI = orange).
* **Fader:** its 14 LEDs show the value as a bar.
* The Core2's own LED bar is unchanged.

## Settings (NVS)

Key `controls`, versioned: for each known unit type, 8 control mappings
(output, channel, CC, min, max, button mode), the last sent value, pickup and
acceleration settings. Page mode needs no settings.

## Files

| File | Purpose |
|------|---------|
| `src/io_task.*` | I/O task: owns `Ex_I2C` and Port B, polls units, hot-plug scan, LED writes |
| `src/grove_units.*` | register access for 8Encoder, 8Angle, Joystick2, Fader, Dual Button |
| `src/control_map.*` | mapping engine: relative/absolute, pickup, acceleration, CC scaling, rate limit (host-tested) |
| `src/controls_page.*` | the Controls page |
| `src/ui.*` | `Page` control API, passing control events to the visible page |
| `oximate.ino` | start the I/O task, drain its page-event queue |

## Milestones

1. **Bring-up:** an 8Encoder on Port A, scan, read counters, keys and switch,
   write LEDs; print to serial. Confirm the register map, 400 kHz, and the
   polling cost.
2. I/O task, the page-event queue, the `Page` control API, and the Synth and
   Clock pages using it.
3. `control_map` with host tests; MIDI mode with the toggle switch; output
   through `MidiTask::send()`.
4. Controls page with learn; settings in NVS; LED value display.
5. 8Angle, Joystick2, Fader and Dual Button support (each optional; only
   units you own get tested on hardware). README and TESTING.md.

## Host tests

* Mapping: relative deltas with acceleration, clamping at min/max, CC
  scaling for a reduced range, pickup (no output until the pot crosses the
  last value, then follows), rate limiting (at most one message per 5 ms per
  control, the last value always sent).
* Button modes: toggle, momentary, note on/off pairs.
* Unit parsing from fake register reads: counters with wrap-around, key and
  switch bits, Joystick2 16-bit axes to 0–127 with a dead zone.

## Risks

* **Register maps:** taken from third-party and M5Stack libraries; firmware
  versions differ (the 8Encoder V2 adds registers). Milestone 1 checks them.
* **Hot-plugging I2C devices** can glitch or lock the bus. The I/O task
  recovers by resetting the `Ex_I2C` driver after repeated errors.
* **Current draw:** LED rings at full brightness draw noticeable current from
  the battery; LED brightness follows the Router page's LED level.
* **Port B conflicts:** a unit on Port B takes GPIO 26, which the stage 20
  notes considered for a built-in DAC output.

## Open questions

1. Which unit(s) would you get: the 8Encoder (recommended), the 8Angle, a
   fader, a joystick?
2. Should the encoders control the visible page, send MIDI CCs, or both
   (switch-selected, as planned)?
3. For MIDI mode: which targets first (M8, Elektron, DFAM via stage 20)?
   That decides the default mappings.
4. Do you want the encoder push buttons to send notes (e.g. to trigger
   samples) or only CC toggles?
