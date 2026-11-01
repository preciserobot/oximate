# Stage 6: Preview synth

> **Status: plan, not implemented.** Written 2026-10-31. Where this plan and the README's
> [Shared infrastructure](../../README.md#shared-infrastructure) section disagree, the README wins:
>
> * The router TX observer is a `MidiOutputs` listener.
> * `Leds::setOverride(first, count)`/`setPixel()` → use the shared LED claim API.
> * Channel 16 stays reserved as the SAM2695 settings channel for all stages.
>
> **Decided 2026-11-01:**
>
> * Elektron gear on DIN OUT responds on all channels, and **program changes on channel 16 would switch Elektron patterns** in Separate mode. Show a warning on the Synth page, and document turning off PC receive on the Elektron or using Bypass mode.
>
> **Decided 2026-11-01:** add an output choice, "synth chip / built-in
> speaker". Speaker mode plays previews with a small software synth on the
> Core2 through its own speaker (see README stage 6), since the SAM2695's
> audio can't reach the Core2. Nothing is sent to DIN OUT in that mode.

Turns the SAM2695 in the Unit MIDI into a controlled sound source:
* choose which channels it plays
* choose the instrument, volume and effects for each layer
* audition sounds from a browser

It also provides `src/sam_synth.*`, the single way any stage talks to the
synth chip. Stages 5 (drum machine) and 7 (ear trainer) build on it.

## What the SAM2695 offers

All of the following comes from the
[Dream SAM2695 datasheet](https://docs.dream.fr/pdf/Serie2000/SAM_Datasheets/SAM2695.pdf)
([product page](https://docs.dream.fr/items.php?item=4)), unless marked
otherwise.

| Feature | Details |
|---------|---------|
| Polyphony | 64 voices without effects; 38 with reverb + chorus. Reverb costs 13 voices, chorus 3, 4-band EQ 8 (NRPN `375Fh` switches each one on or off). |
| Sounds | 128 GM instruments (bank 0), plus an **MT-32 variation bank**: send CC0 = 127, then a program change. |
| Drum kits | Channel 10 by default: Standard (PC 1), Power (17), Brush (41), Orchestra (49), CM-64/32 (127 or 128; the datasheet gives both, so verify). Any part can be switched to drums with GS SysEx `40 1p 15`. |
| Channel CCs | 0 bank, 1 mod, 5/65 portamento, 7 volume, 10 pan, 11 expression, 64/66/67 pedals, **80 reverb type (0–7)**, **81 chorus type (0–7)**, 91 reverb send, 93 chorus send, 120/121/123 |
| NRPN `37xxh` | Master volume `3707h`, EQ `3700–370Bh`, effects/polyphony `375Fh`, SysEx device ID `3757h` (default 20h, which accepts all) |
| GS SysEx `F0 41 00 42 12 40 …` | **Part → MIDI channel assign `40 1p 02 nn`, where nn = 16 means OFF (mutes the part)**; part → drums `40 1p 15`; reverb and chorus levels and times. The checksum is "don't care". |
| Parts | Part 0 = channel 10 (drums), parts 1–9 = channels 1–9, parts 10–15 = channels 11–16 |
| Reset | `FF` (System Reset) and `F0 7E 7F 09 01 F7` (GM reset) restore the power-up state |

The bundled M5-SAM2695 library is not used, because it has problems:
* `setModWheel()` and `setAllInstrumentDrums()` start their SysEx with `B0`
  instead of `F0`, so they don't work and put stray bytes on DIN OUT.
* `reset()` sends `FF` (System Reset), which external gear on DIN OUT also
  receives.

## The wiring problem and the recommended design

The Core2's output drives **both** the SAM2695 and DIN OUT in Separate mode
(see Unit MIDI wiring). So:

1. **The SAM2695 plays everything that is sent to DIN OUT**, on all 16
   channels. That's today's behaviour.
2. **Every message meant for the synth also reaches external gear.** A program
   change to set a piano on channel 1 also changes the patch on the external
   synth listening to channel 1.

There are three options:

| Option | How | Pros | Cons |
|--------|-----|------|------|
| A. Bypass workflow | Set the switch to Bypass. DIN OUT is then a hardware thru, and the Core2 output only reaches the SAM2695. | Complete isolation. | USB MIDI no longer reaches DIN OUT, so stage 1 merging is lost. Software can't detect the switch position. |
| B. Reserved channel only | The synth only plays on one channel (e.g. 16) that no external gear uses. | Simple. | Previewing channel 1 means duplicating its traffic onto channel 16, which doubles DIN bandwidth. |
| **C. Part listen map + config channel (recommended)** | Use the GS part-assign SysEx: mute every SAM part except the ones you choose, and point each one at the output channel it should play. Send all configuration (program, volume, pan, sends) on a reserved **config channel** while the part is temporarily assigned to it, then move the part to its listen channel. | No duplicated traffic. The synth plays exactly the chosen channels. External gear only ever sees messages on the config channel, plus a few GS SysEx messages. | Relies on a part keeping its program when it is reassigned (standard GS behaviour; verify in milestone 1). GS SysEx reaches DIN gear: Roland GS devices normally use device ID 10h and ignore the device-00 messages used here (verify). |

**Recommendation: C**, plus a **Wiring** setting (Separate or Bypass) that you
set to match the switch:
* **Separate (default):** design C with a config channel (default 16). No
  universal SysEx and no `FF` is sent. Master volume and effects use NRPN and
  CCs on the config channel; the NRPN select is then reset with a null RPN
  (CC101/100 = 127) so external gear doesn't pick up stray data entries.
* **Bypass:** the same code. Everything only reaches the SAM2695, so the
  config channel can be any channel. The Router page labels `USB>DIN` as
  `USB>SAM`.

## Features

* **Listen mode**
  * **All**: power-up behaviour. The synth plays every channel; this is the
    default on first boot, matching stages 1 and 2.
  * **Layers**: only the configured layers sound.
  * **Off**: the synth is silent.
* **4 melodic layers + 1 drum layer.** Each has:
  * a listen channel (off or 1–16)
  * a bank (GM or MT-32) and a program, or a drum kit
  * volume, pan, reverb send and chorus send

  Two layers on the same channel stack, for example piano and strings.
* **Instrument browser:** 16 GM families × 8, an MT-32 bank toggle, and the
  drum kits. Tapping a name auditions it (C4, then a C major triad).
* **Global settings:** master volume, reverb type and level, chorus type and
  level, and a **voices** mode:
  * Full: reverb + chorus, 38 voices
  * Lite: chorus only, about 61 voices
  * Dry: no effects, 64 voices
* **Follow:** program changes from your controller on a layer's channel still
  change that SAM part, as you'd expect. The page notices them in the outgoing
  stream and updates the instrument it shows.
* **Self-healing:** if a System Reset or a GM/GS reset passes through to DIN
  OUT, or you press Resend, OxiMate re-applies the full synth state. It does
  the same at boot, because the SAM2695 forgets everything when powered off.

## Synth page

Tap a cell to select it (shown inverted). Then:
* **A** and **C** decrease and increase the selected value; holding repeats.
* **B** depends on the selected cell: on an instrument it opens the
  instrument browser (**Browse**); elsewhere it plays a test chord on that
  layer (**Test**).

```
┌────────────────────────────────────────┐
│Synth>   USB 1C75:0288   DIN 120.0 PLAY │
│Mode: Layers           Cfg ch 16  Voc 38│  tap Mode to cycle All/Layers/Off
│ # Lstn Instrument          Vol Pan Rev │
│ 1 ch 1 001 Grand Piano     100  64  40 │
│ 2 ch 1 049 String Ens 1     80  64  60 │  layers 1+2 stack on channel 1
│ 3 off  081 Lead 1 Square    90  64  20 │
│ 4 off  MT 045 Saw Wave      90  64  20 │  MT = MT-32 bank
│ D ch10 Kit Standard        110  64  30 │
│Master 100  Rev Hall2  64  Cho Cho3  0  │
│Wiring: Separate          [Resend]      │
│      -          Browse          +      │  touch buttons A / B / C
└────────────────────────────────────────┘
```

**Browser** (a sub-view of the same page; `Back` returns):

```
┌────────────────────────────────────────┐
│Synth>   Layer 1 · GM bank              │
│[Pno][Chr][Org][Gtr][Bas][Str][Ens][Brs]│  16 GM families
│[Red][Pip][Led][Pad][SFX][Eth][Prc][FX ]│
│ 001 Acoustic Grand Piano               │
│>002 Bright Acoustic Piano              │  tap = audition
│ 003 Electric Grand Piano               │
│ 004 Honky-tonk Piano                   │
│ 005 Electric Piano 1                   │
│ 006 Electric Piano 2                   │
│ 007 Harpsichord                        │
│ 008 Clavi                              │
│    Back         MT-32          Select  │  B toggles the GM / MT-32 bank
└────────────────────────────────────────┘
```

For the drum layer, the family grid is replaced by the kit list.

## LED bar

While the Synth page is visible, LEDs 0–4 show layers 1–4 and the drum
layer. Each flashes in its note's colour when a note on its listen channel
goes out. LEDs 5–9 keep the default DIN activity display. On other pages the
default activity display (stage 2) is unchanged.

This needs a small addition to `Leds`, shared with later stages:
`setOverride(int first, int count, bool)` and `setPixel(int, RGBColor)`.
`src/leds.h` already mentions `setPixel()` but doesn't implement it yet.

## `src/sam_synth.*` API (owned by this stage)

```cpp
enum class SynthWiring : uint8_t { Separate, Bypass };
enum class SynthListen : uint8_t { All, Layers, Off };
enum class SynthVoices : uint8_t { Full, Lite, Dry };     // NRPN 375Fh presets

struct SynthPart                                        // one of the 16 SAM parts
{
  uint8_t listen = 0;        // 0 = muted, 1..16 = MIDI channel it plays
  bool drums = false;        // GS part -> rhythm
  uint8_t bank = 0;          // 0 = GM, 127 = MT-32 variation
  uint8_t program = 0;       // 0..127 (drums: kit program)
  uint8_t volume = 100, pan = 64, reverb = 40, chorus = 0;
};

class SamSynth
{
public:
  void begin(DinMidi &out);                 // apply the saved state (boot)
  void restore();                           // re-send the complete state
  void onSent(const MidiMessage &msg);      // Router TX observer: resets, followed PCs

  // Parts: stages claim parts instead of hard-coding numbers
  int8_t claimPart(const char *owner);      // -1 if none free
  void releasePart(int8_t part);            // mutes it again
  void configurePart(int8_t part, const SynthPart &p);  // via the config channel
  const SynthPart &part(int8_t part) const;

  // Global (sent as NRPN/CC on the config channel; never universal SysEx)
  void setMasterVolume(uint8_t level);      // NRPN 3707h
  void setReverb(uint8_t type, uint8_t level);  // CC80 + GS 40 01 33
  void setChorus(uint8_t type, uint8_t level);  // CC81 + GS 40 01 3A
  void setVoices(SynthVoices v);            // NRPN 375Fh
  void setListen(SynthListen mode);
  void setConfigChannel(uint8_t ch);        // 1..16
  void setWiring(SynthWiring w);

  // Sound
  void playOnPart(int8_t part, uint8_t note, uint8_t vel);  // on the config channel
  void stopOnPart(int8_t part, uint8_t note);
  void silencePart(int8_t part);            // note-offs from NoteTracker, then CC120 on the config channel
};
```

**How a part is configured** (in `configurePart` and when auditioning):
1. Mute any other part listening on the config channel (GS `1q 02 10`).
2. Assign part p to the config channel.
3. Send CC0 (bank), the program change, CC7, CC10, CC91 and CC93 on the
   config channel.
4. Assign part p to its listen channel, or OFF.
5. Restore the parts muted in step 1.

GS messages are written with a correct Roland checksum even though the SAM
ignores it, so that real GS gear downstream doesn't see malformed data.

**Part allocation:**

| Part | Use |
|------|-----|
| 0 | Drum layer (and the stage 5 drum machine when active) |
| 1–4 | Melodic layers 1–4 |
| 5–14 | Free, for `claimPart()` (e.g. stage 7) |
| 15 | Audition part (normally muted) |

In **All** mode, parts follow the GS default map (part n listens on its
default channel). Configuration still goes through the config channel.

**Output path:** `SamSynth` writes through a new `DinMidi::sendRaw(const
uint8_t*, uint16_t)`. These writes bypass the Router, so the channel filters
and remapping don't apply to them. Once stage 8 moves DIN output into the
MIDI timing task, these writes go through that task's output queue instead.

## Integration

* **Router** (`src/router.*`): add `setTxObserver(void (*)(const
  MidiMessage&))`, called for each message actually sent to DIN, after any
  channel remap. `SamSynth::onSent` uses it to:
  * detect `FF` and GM/GS reset SysEx, and schedule `restore()` 50 ms later
  * track followed program changes
  * drive the Synth-page LEDs
* **Note tracking:** `silencePart()` asks stage 16's `NoteTracker` for notes
  held on the part's listen channel and sends their note-offs. External gear
  received those notes too, so the note-offs are correct for it as well.
* **Settings:** a separate NVS key `"synth"` holds a `SynthConfig` with its
  own `kVersion`:
  * wiring, listen mode and config channel
  * the 5 `SynthPart`s for the layers
  * master volume, reverb, chorus and voices settings

  `RouterConfig` is not changed (it stays version 1). Saves are delayed by
  2 s, as for the router.
* **New files:**
  * `src/sam_synth.*`
  * `src/gm_names.h`: GM and MT-32 names (128 each), 16 family labels, the
    kit list; constant data in flash, about 5 KB
  * `src/synth_page.*`
* **`oximate.ino`:** add `SamSynth samSynth;` and call `samSynth.begin(dinMidi)`
  after the router. Add the page with `ui.addPage(synthPage)` and register the
  router TX observer.

## Milestones

1. **Verify on hardware** with a throwaway test mode: part-assign OFF mutes a
   part; a program survives reassigning the part; the checksum is ignored;
   NRPN 3707h works on channel 16; which program number gives the CM-64 kit;
   voice counts for each 375Fh preset.
2. **`SamSynth` encoding**, with host unit tests for the byte sequences:
   part ↔ channel mapping, GS checksum, NRPN + null RPN, and the configure
   sequence.
3. **Router TX observer** and `restore()` on boot and on resets.
4. **Synth page**: the layer table, select-and-adjust editing, test chords.
5. **Instrument browser** and auditioning.
6. **Global settings**: effects, voices mode, wiring setting, Resend.
7. **Saving**, Leds override and LEDs, README update and commit.

## Test plan

* **Host:** golden byte tests for every `SamSynth` call, plus the
  `onSent()` reset detection.
* **Isolation:** connect an external synth to DIN OUT listening on channel 1,
  with channel 16 unused. Browsing and setting SAM instruments must **not**
  change the external patch. Monitor the DIN OUT line with a second MIDI
  interface and confirm the only traffic is on channel 16 plus GS SysEx.
* **Listen map:**
  * Layers mode with layer 1 on channel 1: a keyboard on channel 1 sounds; a
    keyboard on channel 2 is silent on the SAM but still reaches DIN OUT.
  * Two layers on channel 1 stack.
* **Self-healing:**
  * Send `FF` from DIN IN: the configuration is back within about 100 ms.
  * Power-cycle the device: the state is restored from NVS.
* **Load:** a full `restore()` burst (16 × 11-byte SysEx + about 40 CCs, roughly
  300 bytes ≈ 100 ms on DIN). Check that live notes are not delayed badly.
  If they are, spread the burst over several loop iterations.
* **Polyphony:** a sustained chord with sustain pedal in Full and Dry modes.

## Risks and open questions

* **Unverified chip behaviour:** whether a part keeps its program after
  reassignment; the CM-64 kit number; whether the checksum is ignored. All are
  covered by milestone 1. The fallback is to configure on the listen channel
  directly and warn you that external gear on that channel will see it.
* **Roland GS gear on DIN OUT:** if a device accepts device ID 00, the
  part-assign SysEx would mute its parts too. Mitigation: a "No GS SysEx to
  DIN" setting that falls back to option B (reserved channel only).
* **Config channel conflict:** you must pick a channel no external gear uses.
  Default 16; shown on the page.
* **Program changes and controllers from your gear** on a layer's channel
  change the SAM part ("follow"), so the shown state can drift. The TX
  observer keeps the display in sync, and Resend re-applies the saved state.
* **Stage 5 sharing the drum part:** the drum machine may want its own
  channel and kit. `claimPart()` lets it take another part instead of part 0.
