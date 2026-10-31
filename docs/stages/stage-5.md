# Stage 5: Drum machine

> **Status: plan, not implemented.** Written 2026-10-31. Where this plan and the README's
> [Shared infrastructure](../../README.md#shared-infrastructure) section disagree, the README wins:
>
> * `MidiSource::Internal` / `Router::sendInternal()` → use the shared `MidiOutputs::send()` with an `Internal` origin.
> * The SAM2695 drum voice is configured through stage 6's `SamSynth` part API.

A pocket drum machine. It plays the SAM2695's General MIDI drum kit and the
same pattern on DIN OUT, so external drums can follow. It's driven by the
stage 8 clock engine, so it follows internal or external clock and uses the
global swing.

## Features

* **8 lanes × 16 steps** per pattern. Pattern length is 1–16 steps.
* Each step is **off, on or accent**. Each lane has a normal velocity and an
  accent velocity.
* **Per lane:** GM drum note, MIDI channel (default 10), mute, solo.
* **16 patterns per bank.** A pattern switch is queued and happens at the end
  of the current pattern, so it never jumps mid-bar.
* **Chaining:** a list of up to 16 patterns played in order and looped.
* **Kit:** an optional program change on channel 10 selects a SAM2695 drum
  set (see Sound source).
* Swing and tempo come from the clock engine. The drum machine has no tempo
  of its own.
* **Later (not in the first version):** per-lane length (polymeter), step
  probability, ratchets, and per-step velocity editing.

## Sound source (SAM2695)

* **Channel 10** is the GM drum channel, notes 35–81 in the GM percussion
  map. A **program change on channel 10 selects a drum set**. The chip has
  more than one set, including an MT-32-compatible one; the exact list needs
  verifying on the hardware.
  ([Dream firmware notes](https://docs.dream.fr/pdf/Serie2000/Firmwares/Firm2635.pdf),
  [SAM2695 overview](https://docs.dream.fr/items.php?item=4))
* **Polyphony:** 64 voices without effects, 38 with effects
  ([datasheet copy](https://www.cpcwiki.eu/imgs/0/0d/SAM2695.pdf)). That is
  plenty for 8 lanes.
* **Latency** from UART byte to sound isn't documented. Measure it in
  milestone M1 (see Test plan).
* The M5-SAM2695 library has no drum-kit list, only melodic program names and
  the `SynthDrum` program. A kit/note name table goes in `src/drum_kits.h`.

Default lanes (GM notes):

| Lane | Name | Note | Lane | Name | Note |
|------|------|------|------|------|------|
| 1 | BD Kick       | 36 | 5 | CP Clap      | 39 |
| 2 | SD Snare      | 38 | 6 | LT Low tom   | 45 |
| 3 | CH Closed hat | 42 | 7 | HT High tom  | 50 |
| 4 | OH Open hat   | 46 | 8 | CY Crash     | 49 |

* Open hat and closed hat form a **choke pair**: a closed-hat hit sends a
  note-off for the open hat. This matters for external gear only; the
  SAM2695 kit handles it internally.

## Output routing

* Drum notes are generated internally. They **don't pass through the Router's
  input filters**. They go to:
  * **DIN OUT + SAM2695.** The Core2's output drives both (see Unit MIDI
    wiring), so the internal synth and external gear always receive the same
    drums in Separate mode.
  * **USB device:** optional, off by default.
* **To hear drums on the SAM2695 only:** set the Unit MIDI switch to
  **Bypass**. The Core2's output then only reaches the SAM2695, and DIN OUT is
  a hardware thru of DIN IN.
* **To use external drums only:** set the lane channels to a channel the
  SAM2695 ignores (anything but 10 plays a melodic sound), or turn the SAM's
  channel volume down via `src/sam_synth.*` (stage 6).
* The kit program change is **opt-in** (setting "Send kit PC"), so it doesn't
  change programs on external gear that listens on channel 10.
* **Note-offs** are sent half a step after each note-on, scheduled through the
  clock engine. The GM kit mostly ignores them, but external drum modules and
  synths need them. Every sent note is registered with `src/note_tracker.*`
  (stage 16) so panic and stop can release it.
* On **Stop**, all held drum notes are released through the note tracker.

### Code changes needed outside this stage

* A third source, **`MidiSource::Internal`**, for messages OxiMate generates.
  The stage 8 clock engine needs it too, so agree on it there.
  * Arrays currently indexed by `MidiSource` must ignore it or grow:
    `ClockTracker::_state[2]`, `Leds::_flash[2]`,
    `RouterConfig::channelMask[2]` and `forceChannel[2]`.
  * The Monitor shows internal messages in a third colour (green), with
    display off by default to avoid flooding the log.
* **`Router::sendInternal(const MidiMessage &msg, bool toDin, bool toUsb)`**
  sends to outputs without applying the input filters.

## Touch UI

The screen is 2" (about 41 × 31 mm), so a 16-column grid would give cells
about 2 mm wide, too small for fingers. The design instead shows **one lane
at a time as 16 large pads**, with an overview of all lanes underneath. This
is the familiar groovebox layout: select an instrument, then 16 step buttons.

Grid view (default):

```
┌────────────────────────────────────────┐
│Drums>   USB --          INT 120.0 PLAY │  header (tap: next page)
│[BD][SD][CH][OH][CP][LT][HT][CY]        │  lanes: tap = select + preview
│┌──┐┌──┐┌──┐┌──┐┌──┐┌──┐┌──┐┌──┐        │
││ 1││ 2││ 3││ 4││ 5││ 6││ 7││ 8│        │  16 step pads, 2 × 8, 40×44 px
│└──┘└──┘└──┘└──┘└──┘└──┘└──┘└──┘        │  tap = on/off, hold = accent
│┌──┐┌──┐┌──┐┌──┐┌──┐┌──┐┌──┐┌──┐        │  playhead = white outline
││ 9││10││11││12││13││14││15││16│        │
│└──┘└──┘└──┘└──┘└──┘└──┘└──┘└──┘        │
│▪···▪···▪···▪··· BD                     │  overview: 8 lanes × 16 steps
│··▪···▪···▪···▪· SD   (6 px rows)       │  tap a row = select that lane
│ ...                                    │
│P01  len 16  swing 54%   M:-- S:--      │  status line
│   Play        Mute        View>        │  A / B / C
└────────────────────────────────────────┘
```

* **Step colours:** off = dark grey, on = lane colour, accent = bright lane
  colour with a white dot. A muted lane is dimmed.
* **A Play/Stop:** starts or stops the clock engine's transport. When
  following external clock it only arms or disarms the drum machine, because
  transport belongs to the external master.
* **B Mute:** mutes or unmutes the selected lane. Hold B for solo.
* **C View>:** cycles the views Grid → Sound → Pattern.

Sound view (edit the selected lane):

```
┌────────────────────────────────────────┐
│Drums>   USB --          INT 120.0 PLAY │
│[BD][SD][CH][OH][CP][LT][HT][CY]        │
│ Note   [-]  36 Bass Drum 1   [+]       │  GM name from the note table
│ Vel    [-]  100              [+]       │
│ Accent [-]  127              [+]       │
│ Chan   [-]  10               [+]       │
│ Kit    [-]  0 Standard       [+]       │  program on ch 10 (if enabled)
│ [Send kit PC: off] [USB out: off]      │
│   Play        Audit       View>        │
└────────────────────────────────────────┘
```

* The `[-]` and `[+]` chips are 48×26 px. Hold them to repeat.
* **B Audit:** plays the lane's sound once, so you can hear it while choosing
  a note.

Pattern view:

```
┌────────────────────────────────────────┐
│Drums>   USB --          INT 120.0 PLAY │
│ [01][02][03][04]   Length [-] 16 [+]   │  tap = queue the pattern
│ [05][06][07][08]                       │  hold = copy here
│ [09][10][11][12]   [Chain: off]        │
│ [13][14][15][16]   [Clear pattern]     │
│ Chain: 01 01 02 03                     │  tap a slot while chain is on
│ Bank: /drums/bank01.json               │
│   Play        Save        View>        │  B = save bank to SD
└────────────────────────────────────────┘
```

* **Pattern slot states:** playing = green, queued = blinking, has content =
  lane-coloured edge, empty = grey.
* **Chain on:** tapping slots appends them to the chain. Tapping "Chain: off"
  clears it.
* **Load and choose bank** (files on SD) come in milestone M5. Until the SD
  card works, the current bank lives in NVS.

## LED bar

There are two strips of 5 (see LED bar). The proposed mapping shows the exact
16th-note position:

| LEDs | Meaning |
|------|---------|
| Left 1–4 | Beat in the bar (1–4). The current beat is lit, beat 1 in a brighter colour |
| Left 5 | Chain position: flashes when the next pattern starts |
| Right 1–4 | 16th within the beat (1–4) |
| Right 5 | Hit flash in the colour of the loudest lane that just triggered (accent = full brightness) |

* For pattern lengths other than 16, the playhead wraps at the pattern
  length. Beats are still counted in 4/4 from the clock engine.
* The drum machine takes over the LED bar while its page is visible. Other
  pages get the input-activity display back.
* Which physical strip is "left" is still to be verified (see the stage 2
  hardware checks).

## Code structure

| File | Purpose |
|------|---------|
| `src/drum_machine.*` | Data model, playback on clock-engine steps, note-off scheduling, output |
| `src/drum_page.*`    | The Drums page: Grid, Sound and Pattern views, touch handling |
| `src/drum_kits.h`    | GM percussion note names (35–81) and SAM2695 drum set names |
| `src/drum_storage.*` | Saving and loading banks in NVS and as JSON files on SD (ArduinoJson) |

Data model (one bank is under 1 KB):

```cpp
struct DrumLane    { uint8_t note, velocity, accentVelocity, channel; bool muted, solo; };
struct DrumPattern { uint16_t on[8], accent[8]; uint8_t length; };   // bit n = step n+1
struct DrumBank    { uint8_t version, kitProgram; bool sendKitPc, usbOut;
                     DrumLane lanes[8]; DrumPattern patterns[16];
                     uint8_t chain[16], chainLength; };
```

### Interface needed from `src/step_sequencer.*` (stage 4)

The drum machine should reuse stage 4's sequencer core rather than run its
own. It needs:

* A track/step store with **gate and velocity per step**. Drums use 8
  tracks × 16 steps, with accent stored as velocity.
* `setLength(track or pattern, n)`
* **Position:** `reset()`, `advance()` called once per 16th note from the
  clock engine, and `position()` (current step).
* A **trigger callback** `(track, step, velocity, timeMicros)`.
* A **queued pattern switch** that takes effect at the pattern end, and
  chain support. If stage 4 doesn't provide chaining, the drum machine wraps
  it.
* Thread-safe step edits from the UI core. A single bitmask or byte write is
  atomic; multi-field changes, such as a pattern switch or a lane setting,
  happen under a `portMUX` spinlock.

If stage 4 hasn't been built when stage 5 starts, write the minimal version
of `step_sequencer.*` with this interface first. Stage 4 then extends it.

### Interface needed from `src/clock_engine.*` (stage 8)

* **Step callback** every 6 ticks (one 16th) with swing already applied,
  called on the MIDI timing task (second core).
* **Transport callbacks:** start (reset to step 1), stop (release notes),
  continue, and song position (seek to `position mod length`).
* **Scheduled sends:** schedule a note-off N µs or N ticks after a note-on.
* **Transport state and BPM** for the status line and header.

### Other shared modules

* `src/sam_synth.*` (stage 6): drum set program, channel 10 volume and
  reverb. Sound-view previews go through it.
* `src/note_tracker.*` (stage 16): register every note-on and note-off sent.
  Stop and panic release through it.

### Thread model

* **Playback** runs in the clock engine's callbacks on the timing task, and
  only reads the pattern.
* **The page** edits the pattern on the UI core (Arduino loop). It reads the
  current step and transport through atomics or the clock engine's getters,
  and redraws the pads and LEDs at most once per frame (40 ms). The playhead
  outline is the only thing that redraws on every step.

## Persistence

* **NVS:** the current bank (under 1 KB, one blob with a version byte) and
  the selected lane and pattern.
  * Saves are delayed by 2 s, like the Router, so a burst of edits causes one
    flash write.
  * No save happens while playing unless the edits stop for 2 s.
* **SD:** `/oximate/drums/bankNN.json`, written with ArduinoJson so it's
  readable and editable on a computer.
  * Save by pressing B in the Pattern view. Load comes from a list in that
    view (milestone M5).
  * SD writes only happen while stopped, because the SD card shares the SPI
    bus with the screen and the USB host module (see Architecture).

## Milestones

1. **M1 Sound check.** A test button plays each default lane on channel 10.
   Measure the SAM2695 latency and confirm program changes select drum sets.
   Build the note name table.
2. **M2 Playback.** Data model plus `step_sequencer` (minimal version if
   stage 4 isn't there yet), driven by the clock engine. A hard-coded pattern
   plays on the SAM2695 and DIN OUT, with note-offs and the note tracker.
3. **M3 Grid view.** Lane select, 16 pads (tap and hold), playhead, overview,
   Play/Mute buttons, and the LED bar mapping.
4. **M4 Sound and Pattern views.** Lane settings, 16 patterns, queued
   switching, chain, length, NVS save.
5. **M5 SD banks.** Save and load JSON, a bank list, optional USB output,
   choke pair, and the kit program change setting.
6. **M6 (optional).** Per-lane length, probability, ratchets.

Commit after each milestone. The stage is ✅ in the roadmap after M4.

## Test plan

* **Host tests** (as for the stage 1 parsers): sequencer stepping, pattern
  length wrap, queued switch only at the pattern end, chain order, song
  position seek, choke note-offs, JSON round trip.
* **Timing:** run the drum machine at 120 BPM, with OxiMate as clock master
  and also following external clock. Record DIN OUT on a computer (MIDI
  monitor with timestamps) or use the stage 9 clock analyser. Step jitter
  must stay under 1 ms while the screen redraws and the user taps pads.
* **SAM2695 latency:** send a note on DIN OUT to an audio interface's MIDI
  input and record the SAM's audio output on a second channel; the latency is
  the gap between the two.
* **Load:** all 8 lanes on every step at 200 BPM is 16 notes per step, about
  48 bytes every 75 ms. This is well under DIN's ~3000 bytes/s, but check
  that it stays under together with merged USB traffic.
* **Persistence:** edit, power-cycle, and confirm the pattern is restored.
  Confirm SD save/load works without glitches while stopped.
* **Bypass/Separate:** confirm the documented SAM-only and both-outputs
  behaviour on the real Unit MIDI.

## Risks and open questions

* **Touch precision:** 40×44 px pads are about 5 × 5.5 mm. Holding for
  accent may be awkward. Fallback: an "Accent" mode toggle on B.
* **Shared wire:** the SAM2695 and external gear always get the same drums in
  Separate mode. Is Bypass mode (SAM-only) plus a channel convention good
  enough, or should OxiMate have a "drums to SAM only" option? A software
  option isn't possible with this wiring.
* **Drum sets:** the exact list of SAM2695 drum set program numbers needs
  verifying on the hardware.
* **Dependencies:** this stage needs stage 8 (clock engine, timing task) and
  ideally stage 4's sequencer core. Building stage 5 earlier means writing
  the minimal versions of both first.
* **`MidiSource::Internal`** changes several arrays and the Monitor. Agree on
  it with stage 8 before implementing.
* **For the user:** are 8 lanes and 16 steps right, or do you want 32 steps
  (two pages of pads)? Should the drums also be playable live from a USB pad
  controller (incoming notes on channel 10 trigger lanes and can be recorded
  into steps)?
