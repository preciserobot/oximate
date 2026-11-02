# Stage 20: DFAM MIDI-to-CV

> **Status: plan, not implemented.** Written 2026-11-02. Where this plan and the README's
> [Shared infrastructure](../../README.md#shared-infrastructure) section disagree, the README wins.
> Builds on the README's stage 20 feasibility notes and shares the Port A I2C bus with
> [stage 19](stage-19.md) (extra controls).

OxiMate plays the Moog DFAM, which has no MIDI, through its patch bay:

* **Steps from MIDI notes:** notes C1–G1 (MIDI 24–31) play steps 1–8.
* **The DFAM's sequencer on OxiMate's clock**, including start and stop.
* **MIDI CCs to CV:** filter, decays, noise, velocity, and 1 V/oct pitch for the VCOs.

This needs a small **CV board** on Port A (I2C) and two Core2 GPIOs for gates.

## What the DFAM accepts (manual)

From the DFAM manual's patch bay overview
([manual PDF](https://cdn.inmusicbrands.com/Moog/DFAM/DFAM_Manual.pdf), quoted through
[ManualsLib pages 24–29](https://www.manualslib.com/manual/1351954/Moog-Drummer-From-Another-Mother.html?page=29)):

| Input | Range | Notes |
|-------|-------|-------|
| TRIGGER | 0 to +5 V pulse or gate (+10 V tolerant) | fires all three envelopes **at the current step's velocity**, without advancing |
| ADV / CLOCK | 0 to +5 V clock (+10 V tolerant) | each **rising edge advances one step**; while it's patched, the TEMPO knob is ignored |
| RUN / STOP | 0 to +5 V gate (+10 V tolerant) | +5 V plays from the current step, 0 V stops there (**no reset to step 1**) |
| VELOCITY | 0 to +5 V | sets the envelopes' maximum amplitude |
| VCA CV, NOISE LEVEL, 1>2 FM AMT | 0 to +8 V | summed with their knobs |
| VCA / VCF / VCO DECAY, VCF MOD | −5 to +5 V | VCF MOD replaces the noise as the filter's modulation source |
| VCO 1 CV, VCO 2 CV, TEMPO | −5 to +5 V, 1 V/oct | |

| Output | Range | Use here |
|--------|-------|----------|
| TRIGGER | 0 to +5 V, ~1 ms pulse per played step | optional **step follower** input for OxiMate (see below) |
| PITCH | −5 to +5 V | not used |
| VELOCITY | 0 to +5 V | not used |

**Consequences for the design:**

* The manual gives **no reset input and no gate threshold.** To get to a step,
  you count steps and send pulses until you reach it.
  * A commercial DFAM synchronizer, the
    [VFAM](https://modulargrid.net/e/modules/view/58354), does exactly this: it returns the
    DFAM to step 1 "via fast pulse" through ADV/CLOCK.
  * Owners report the DFAM starts one step early when synced (step 1 "always seems a step out"),
    so the step offset must be adjustable
    ([Moog forum](https://forum.moogmusic.com/viewtopic.php?p=186193),
    [Elektronauts](https://www.elektronauts.com/t/syncing-dfam-with-ot-mpc/57511/11)).
* **Driving gates at 5 V is safest.** The Core2's 3.3 V GPIO might be enough, but the
  threshold isn't documented; Moog's Mother-32 figure is about +3.2 V.
* **Only part of the 0 to +8 V range is reachable.** A ±5 V board covers −5..+5 V CVs fully,
  but the 0..+8 V inputs only up to about +4.8 V. That's fine for modulation; the knobs supply
  the rest.

## Making steps silent while jumping

ADV/CLOCK both advances the step and plays it. A burst of pulses to reach step 6 from step 2
therefore also plays steps 3, 4 and 5. Three options, to be tried in M0/M3:

| Option | How | Cost |
|--------|-----|------|
| A Fast burst | pulses 0.5–1 ms apart; the envelopes restart so quickly that only the last hit is heard | free; may click |
| **B Silent burst + trigger (preferred)** | VELOCITY CV = 0 V during the burst (envelopes at zero amplitude), then set VELOCITY to the note's velocity and fire TRIGGER | needs the CV board; MIDI velocity is respected |
| C Run gated | RUN/STOP low during the burst | only works if ADV/CLOCK advances while stopped (to verify) |

## Hardware

### Block diagram

```
 Core2 / GO Bottom2
 ┌──────────────┐  Port A (Grove: 5V, GND, G32 SDA, G33 SCL)
 │  MIDI task   │──────────────┬──────────────────────────► stage 19 units
 │              │              │
 │  G26 (Port B)├──► gate buf ─┼──► ADV / CLOCK
 │  G27 (M-Bus) ├──► gate buf ─┼──► TRIGGER
 │  G36 (Port B)│◄── divider ◄─┼─── DFAM TRIGGER OUT (step follower)
 └──────────────┘              ▼
                    ┌──────────────────────────────────┐
                    │ CV board                         │
                    │  MCP4728 (0x60) ─► 4× op-amp ±5V ├─► CV1..CV4
                    │  LM2664 −5 V    LM4040 2.048 V   │
                    │  74AHCT125 (5 V) gate buffers    ├─► RUN/STOP (optional, see below)
                    └──────────────────────────────────┘
```

### Free Core2 pins

* **Already in use:**
  * USB host: G18, G23, G38 (SPI), G19 (SS), G35 (INT)
  * Unit MIDI: G13 and G14
  * LEDs: G25
  * Speaker: G0, G2, G12
  * Microphone: G34
  * Internal I2C: G21 and G22
  * Port A: G32 and G33
  * Serial: G1 and G3
* **Free for this stage:**
  * **G26** (Port B), an output
  * **G27** (M-Bus), believed free, to be checked against the Core2 schematic in M1
  * **G36** (Port B), input only, so suitable for the step follower

That's two fast outputs: **ADV/CLOCK on G26** and **TRIGGER on G27**. RUN/STOP changes rarely,
so it comes from the board instead (either option works):

* a spare 74AHCT125 channel driven by an MCP23008 I2C expander at 0x20, or
* simpler, a jumper so RUN/STOP stays high (DFAM running), with the ADV/CLOCK pulses alone
  deciding when steps happen.

### CV board schematic (one of four identical CV channels)

```
 +5V (Grove) ──┬───────────────┬──────────── 74AHCT125 VCC
               │               │
            LM2664 ─► −5V     LM4040-2.048 (with 1k from +5V) ─► VMID 2.048V
               │
 MCP4728 VOUTx (0..4.096 V, internal 2.048 V ref, gain 2)
   └─ R2 10k ─┬─────────── (+) TLV2374
              └─ Rg 24k ─ GND
 VMID (2.048 V) ─ R1 10k ─┬─ (−)
                          └─ Rf 24k ─ output
 output = (Rf/R1)·(VOUTx − VMID) = 2.4 × (0..4.096 − 2.048) ≈ −4.9 .. +4.9 V
 output ─ 470R ─┬──► CV jack (3.5 mm tip)
                └─ BAT54S to the ±5 V rails (clamp)

 Gate:  G26 ── 74AHCT125 (5 V supply, TTL-level input sees 3.3 V as high) ── 470R ──► ADV/CLOCK jack
```

* **DAC:** the MCP4728 has 4 channels at 12 bits, I2C at 100/400 kHz/3.4 MHz, a selectable
  internal 2.048 V reference with gain ×1/×2, and LDAC to update all channels together. Its
  address is programmable, default 0x60
  ([datasheet DS22187E](https://www.microchip.com/content/dam/mchp/documents/OTH/ProductDocuments/DataSheets/22187E.pdf),
  [Adafruit guide](https://learn.adafruit.com/adafruit-mcp4728-i2c-quad-dac?view=all)).
  * The **internal reference** keeps CVs independent of the noisy USB/battery 5 V.
  * Gain ×2 gives 0–4.096 V.
* **Op-amp:** a quad rail-to-rail op-amp that accepts 10 V across its supplies (TLV2374,
  2.7–16 V) on ±5 V, from the Grove +5 V and an LM2664 charge-pump inverter.
  * The output swings to within about 0.1–0.2 V of the rails at the DFAM's light load,
    so about ±4.8 V.
* **Resolution:** 9.8 V / 4096 ≈ 2.4 mV per step, about 1/35 of a semitone at 1 V/oct.
  Calibration (below) removes resistor tolerance and the ref/charge-pump errors.
* **Protection:**
  * 470 Ω series resistors; the DFAM's input impedance is high, and calibration absorbs the
    small divider error.
  * Schottky clamps to the rails.
  * Gate outputs are 5 V logic with 470 Ω, within the DFAM's +10 V-tolerant inputs.
* **Step follower:** DFAM TRIGGER OUT (5 V, 1 ms) → 10k/20k divider → G36, interrupt on the
  rising edge.
  * OxiMate can then count the DFAM's own steps even while it runs on its internal clock.
  * It also confirms that a burst landed.

### Parts list (approximate)

| Part | Qty | Approx. cost |
|------|-----|--------------|
| MCP4728 (or Adafruit MCP4728 breakout) | 1 | €2 (€8 breakout) |
| TLV2374 quad rail-to-rail op-amp | 1 | €2 |
| LM2664 (or ICL7660S) charge pump + 2× 10 µF | 1 | €1.5 |
| LM4040-2.048 shunt reference + 1k | 1 | €1 |
| 74AHCT125 quad buffer | 1 | €0.5 |
| 0.1 % resistors (10k, 24k) for the 4 differential amps | 16 | €4 |
| 470 Ω, BAT54S, decoupling caps | ~15 | €2 |
| 3.5 mm jacks (4 CV + 3 gates + 1 follower in) | 8 | €5 |
| Grove cable, perfboard or small PCB, enclosure | 1 | €8 |
| **Total** | | **≈ €25–35** |

**Off-the-shelf alternative:** M5Stack DAC 2 Unit (GP8413: 2 channels, 15 bits, 0–5 / 0–10 V,
on Grove I2C).
* No soldering for the CVs.
* **Unipolar only:** the ±5 V inputs (decays, VCF MOD, pitch) get only 0..+5 V.
* Only 2 channels, and the gate buffers still have to be built.

Good enough to start with, but the DIY board is recommended.

## Software

### Shared Port A bus

* Port A (M5Unified `Ex_I2C`, controller 0, separate from the internal bus) gets a small
  mutex, `I2cBus`, like `SpiBus`.
* **The MIDI task** writes CVs itself, with try-lock and a ~0.25 ms fast write of all
  4 channels. That keeps the CV → trigger order deterministic.
* **Stage 19's I/O task** polls its units in short transactions under the same lock.

### Gates and timing

* `src/dfam_gates.*` sets GPIOs directly (`gpio_set_level`, a few µs) from the MIDI task.
  * Pulse edges are scheduled with `ClockEngine::schedule()`: high for 300 µs, low for
    700 µs by default, adjustable 0.3–5 ms in M0.
* **`ClockOutput::Gate`:** the clock engine's third output. The DFAM's sequencer then follows
  OxiMate's clock at 1/16 or 1/8 (the existing per-output dividers). Start/Stop drive
  RUN/STOP when the board has it.

### Step model (`src/dfam.*`, plain C++ apart from the gate and DAC hooks)

* **State:**
  * `step` (0–7, or "unknown")
  * `offset` (the "one step early" quirk)
  * mode: Clock / Notes / Off
* **Notes mode:** a note on the DFAM channel (default 11, to be agreed), C1–G1 = steps 1–8.
  1. `k = (target − step) mod 8`
  2. option B: VELOCITY CV = 0
  3. `k` ADV pulses
  4. VELOCITY CV = note velocity
  5. a TRIGGER pulse, 1 ms after the last ADV edge
  * With `k = 0` only the TRIGGER fires. A burst in progress is cut short by the next note.
* **Pitch:** notes outside C1–G1 on the DFAM channel set VCO 1 CV (optionally VCO 2), 1 V/oct
  with C3 = 0 V, and play the current step.
* **CC → CV:** each of the 4 channels has a CC number and a range. Defaults:

  | CV | DFAM input | CC |
  |----|------------|----|
  | CV1 | VELOCITY | used by notes mode |
  | CV2 | VCF MOD | 74 |
  | CV3 | VCO 1 CV | pitch |
  | CV4 | VCF DECAY | 71 |

* **Resync:**
  * "Step 1 now" button: press the DFAM's ADVANCE until its LED is on step 1, then tap.
  * "Home" button: with the step follower fitted, OxiMate pulses until it has counted a full
    cycle.
  * The follower also corrects drift when someone presses ADVANCE on the DFAM.

### DFAM page

```
┌────────────────────────────────────────┐
│DFAM>    USB ok  INT 120.0 PLAY  [87%]  │
│ Mode [Notes]  Ch 11  Burst B  Off +1   │
│  [1] [2] [3] [4] [5] [6] [7] [8]       │  current step, tap = jump
│ CV1 Velocity   ██████░░░░  +3.10 V     │
│ CV2 VCF Mod    ███░░░░░░░  −1.25 V CC74│
│ CV3 VCO1 pitch ████░░░░░░  +0.00 V     │
│ CV4 VCF Decay  █████░░░░░  +0.40 V CC71│
│ Follower: synced   Pulse 0.3/0.7 ms    │
│   Step 1      Test 1-8       View      │
└────────────────────────────────────────┘
```

* Buttons: **A** "Step 1 now" (hold: Home), **B** plays steps 1–8 once (test),
  **C** cycles views (Main / CV map / Calibrate).
* LEDs: the 8 steps on LEDs 0–7, LED 8 for the burst, LED 9 for the follower state.

### Calibration (NVS key `dfam_cal`, per channel: offset, gain)

* **Multimeter routine** on the Calibrate view: the channel outputs −4.000 V, then +4.000 V.
  You enter the measured value with A/C, and gain and offset are computed and saved.
* **Pitch check:** with the CV patched into VCO 1 CV, the page alternates C2 and C3 so you can
  check the octave by ear or with a tuner, and fine-tune the gain.

### Settings (NVS key `dfam`, versioned)

* mode, channel, note range
* burst option (A/B/C), pulse timing
* step offset, clock ratio
* CV map (input, CC, min/max per channel)
* whether the follower is fitted, and how RUN/STOP is driven

## Testing without the DFAM

* **Multimeter on each CV jack** at −4, 0 and +4 V after calibration (within ±5 mV), and
  pitch steps of 83.3 mV per semitone.
* **Gates:**
  * an LED with a 1k resistor on each gate output, or a logic analyser or scope, to check
    pulse widths and burst counts;
  * loop ADV/CLOCK back into the follower input (G36) to check that the counted pulses match.
* **Host tests (`make test`):**
  * step distance and wrap-around
  * burst scheduling (edge times, cut-short by a new note)
  * CC → DAC code including calibration
  * the 1 V/oct note → code mapping
  * follower resync logic

## Milestones

1. **M0, one buffered gate (before the CV board).** A 74AHCT125 (or an NPN at 5 V) from G26
   into ADV/CLOCK, plus a test mode on the DFAM page: send `k` pulses at a chosen period.
   Answers:
   * Are skipped steps audible at 0.5 / 1 / 2 ms?
   * What's the shortest period the DFAM accepts?
   * Does ADV/CLOCK advance while stopped?
   * Is the "one step early" offset needed?
2. **M1:** build the CV board, `I2cBus`, MCP4728 driver, Calibrate view; confirm G27 is free.
3. **M2:** Clock mode: `ClockOutput::Gate`, dividers, RUN/STOP.
4. **M3:** Notes mode with silent bursts (option B), velocity, TRIGGER.
5. **M4:** CC → CV map and pitch CV.
6. **M5:** step follower on G36, Home, settings, LEDs, README and TESTING.md.

## Risks

* **Undocumented DFAM behaviour:** the gate threshold, the minimum ADV period, whether skipped
  steps are audible, and whether ADV works while stopped. M0 answers all of these before
  anything else is built.
* **Velocity CV at 0 might not silence the envelopes fully**, e.g. if the jack sums with the
  sequencer's velocity rather than replacing it. Fallbacks: option A or C.
* **Charge-pump noise on the CVs:** use filter caps and an LC filter if it's audible on pitch.
* **Sharing the Port A bus with stage 19 units:** a long encoder transaction delays a CV write.
  Mitigations: the try-lock in the MIDI task, and keeping I/O transactions short.
* **G27 turns out to be used on the Core2:** fall back to TRIGGER via the MCP23008 expander,
  accepting I2C latency (about 0.1 ms) for the trigger.

## Open questions for you

1. Which DFAM CVs matter most for your techno sets? Proposed: velocity, VCF MOD, VCO 1 pitch,
   VCF decay. With a second MCP4728 at 0x61 you could have 8.
2. Build the DIY ±5 V board (recommended, ≈ €30), or start with the M5 DAC 2 Unit
   (unipolar, 2 CVs)?
3. Should OxiMate drive RUN/STOP, or should the DFAM stay running and OxiMate control only
   ADV/CLOCK and TRIGGER?
4. Which MIDI channel should the DFAM listen on (suggested 11), and which device sends it notes
   (Oxi One, M8, Elektron)?
5. Do you want the step follower (one extra cable from DFAM TRIGGER OUT), so the count stays
   right even when you touch ADVANCE on the DFAM?
6. DFAM revision and firmware, and whether you've seen the "one step early" behaviour on yours.
