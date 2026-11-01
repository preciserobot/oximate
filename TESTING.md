# Hardware test checklist

Firmware: `develop` (stages 1, 2, 8 and 16), 2026-11-01.

> Update: the DIN "noise" seen at first was probably the Oxi sending Oxi Split
> data. Pins are back to the original order (RX 13 / TX 14). Re-test the DIN
> items with the Oxi set to normal MIDI out. Swipes now need to be longer
> (about half the screen width).

How to use this file:

* Tick a box with `[x]` when it works.
* Write `FAIL:` plus what happened on the line below an item that doesn't.
* Add notes anywhere (`>` lines are easiest to spot).
* If something freezes, resets or leaves a note stuck, run
  `make log SECONDS=30` while reproducing it and paste the output at the end.

Before starting: set the Unit MIDI's front switch to **Separate**.

Items marked **MUST** have to pass before the next stages (6, 5, 7, 3, 4, 9)
are built on top; the rest can be checked along the way.

## 1. Basics (must pass before moving on)

* [x] The start-up screen (red "oximate" banner) shows for about 3 s, then the Monitor page appears
* [x] Swiping left/right switches pages: Monitor → Router → Clock → Diag (tapping the header also works)
* [x] Notes from a USB keyboard show on the Monitor in cyan
* [x] Notes from DIN IN show on the Monitor in orange
* [x] Both inputs come out of DIN OUT
* [x] Both inputs are audible on the SAM2695 (headphones on the Unit MIDI)
* [x] DIN IN notes reach the USB device
* [x] Monitor buttons: A clears, B pauses/resumes, C shows/hides clock messages
* [x] LEDs flash on incoming notes
  > Which side lights up for USB, and which for DIN: right side = USB, left side = DIN
* [x] Playing fast on both inputs while switching pages: no missing notes, no stuck notes, no freezes

## 2. Router page

* [x] **MUST** Turning a route off (e.g. USB>DIN) stops that traffic
* [ ] A blocked channel stops passing, and its Monitor lines turn grey
* [x] **MUST** Force channel works (e.g. USB>ch5)
* [x] **MUST** Holding a note while changing force channel or blocking its channel: no stuck note
* [x] **MUST** Settings are still there after a power cycle

## 3. Clock page

* [x] **MUST** Start/Stop sends clock to the M8 over USB (the M8 follows the tempo and starts/stops)
* [x] **MUST** Outputs view: switching DIN clock on makes the Elektron follow over DIN
* [x] The tempo chips (±1, ±0.1) and tap tempo (B) change the tempo
* [x] The LEDs show beat and bar while the Clock page is open
* [x] **MUST** Source DIN with the Elektron sending clock: tempo shown, rightmost LED turns blue (locked)
* [x] **MUST** Stopping or unplugging the Elektron's clock: the display shows LOST and OxiMate stops
* [x] Clock settings are still there after a power cycle
* On the clock page the start/stop label does get out of sync with the clock status (does not always update on stopping or starting)

## 4. Panic and stuck notes

* [x] **MUST** Short power-button press: PANIC in the header, LEDs flash red, held notes stop
* [x] Holding the power button ~1.5 s: PANIC ALL, the red LED bar fills over about 2 s
* [x] **MUST** Holding a note on a USB keyboard and unplugging it: the note stops on DIN
* [x] ~~A note held for more than 30 s is released automatically~~ removed (2026-11-01)
  > Is the 30 s auto-release OK for drones/pads you hold on purpose?
  No i think this can be removed as we'll likely use very long pads (notes longer than 30s, using pitchbend modulation for example)

## 5. Diag page

* [x] Channel view shows activity; channels 1–10 light up on the LEDs
* [x] Counters go up while playing; A resets them
* [x] **MUST** System view while playing heavily:
  > UI loop max: ~300000us max, avg ~ 1000-2000us
  > USB bus busy: ~27000 after 30s and increasing (input from DIN and USB)
  > UI events dropped: 0
>
* On the diagnostic page, keys held via USB show in the DIN counter, and notes held via DIN show on both USB and DIN counters
  > Correct behaviour, now labelled "Held at DIN OUT / USB OUT": it counts notes at the outputs.

> Follow-up (2026-11-01): after the bus hand-off fix, please re-check while playing heavily:
> USB gap max (Diag system view, "USB ... gap max"): 50 ms
> UI loop max: ~50000 us
>
> Result after off-screen drawing (507993f), 40 s of heavy playing on both inputs, measured
> over serial: USB gap max 5–6 ms, bus held max 3 ms (content slices), events per loop 0–2 ms,
> nothing dropped. A full screen redraw takes ~70 ms under load (it yields to USB between
> slices). **Passed.**

## 6. Synth page (stage 6)

Headphones on the Unit MIDI. Note the PC/CC warning: with the switch on
Separate, settings go out on channel 16 to DIN gear too.

* [ ] Synth page appears between Router and Clock
* [ ] Mode All: the synth plays every channel as before
* [ ] Mode Layers: layer 1 (ch1, piano) plays from a keyboard on channel 1; a keyboard on channel 2 is silent on the synth
* [ ] Layer 2 also on ch1 with strings: piano and strings stack
* [ ] B on a layer row plays a test chord with that layer's sound
* [ ] Browser (B on an instrument cell): tapping a sound previews it; C selects it; A goes back unchanged
* [ ] MT-32 bank (B in the browser) sounds different from GM
* [ ] Drum layer: each of the 5 kits plays (B on the drum row)
  > Does the CM-64/32 kit sound?
* [ ] Volume, reverb send, master volume, reverb type/level and chorus change the sound
* [ ] Voices Full / Lite / Dry: reverb and chorus switch off in Lite / Dry
* [ ] Mode Off: the synth is silent; DIN OUT still works
* [ ] Settings survive a power cycle (the layers come back after power-on)
* [ ] Panic or a System Reset from your gear: the synth comes back with your layers within a moment
* [ ] Out:Speaker: the Core2's speaker plays the layer channels and the test chord; the chip is silent; switching back to Out:Chip restores the chip
  > Did any setting change reach the Elektron/Oxi on channel 16?

## 8. Ear trainer (stage 7)

Synth output on "Chip" (questions on the SAM2695, channel 15) unless noted.

- [ ] The Ear page appears after Clock
- [ ] Intervals: B plays a question; tapping the right answer turns it green, a wrong one red; the result line names the notes
- [ ] Answering on a keyboard works in any octave (play the two notes of the interval)
- [ ] Chords: playing the chord on a keyboard in any inversion counts as correct
- [ ] Degrees: the I–IV–V–I cadence plays, then the note; playing the note answers
- [ ] Melody: play the melody back; the prompt counts up (1/3, 2/3 ...)
- [ ] A replays, B skips (counts as wrong, shows the answer), C cycles the level (1–4, Auto)
- [ ] LEDs flash green/red and show the streak; the vibration motor pulses (switchable)
- [ ] Statistics and levels survive a power cycle
- [ ] Explorer: tapping a root or quality plays the chord; B steps through inversions; C switches block/arpeggio
- [ ] Explorer: a chord played on a keyboard is named ("You played: ...")
- [ ] Leaving the page mid-question leaves no hanging notes
- [ ] Synth output on "Speaker": questions play on the built-in speaker, nothing on DIN OUT
  > Did channel 15 notes reach any of your DIN gear?

## 9. Clock analyser (stage 9)

The Analyse page sits between Clock and Diag. Measurements run in the
background; A resets them.

* [ ] Elektron sending clock into DIN: the DIN column shows BPM, interval, jitter; status STABLE or JITTERY
  > DIN jitter sd and max dev at 120 BPM:
* [ ] M8 sending clock over USB: the USB column fills in the same way
  > USB jitter sd, and "B" (batched ticks) on Ticks/run?
* [ ] Stopping the clock: the status goes to -- after 2 s; Transport shows STOP
* [ ] Pulling the cable mid-run and plugging it back: Dropouts counts it; the LEDs on that side turn red for 2 s
* [ ] Start / Stop / Continue from the sequencer: Transport and Ticks/run follow
* [ ] Histogram view (B): a single peak around 0 for a steady clock
* [ ] Drift view: a flat line at a fixed tempo; changing the tempo on the sequencer shows a step
* [ ] Phase view: with both clocks at the same tempo (e.g. OxiMate's clock out to the Elektron and back in, or M8 and Elektron in sync), the DIN-USB offset is shown
* [ ] LEDs while the page is open: right side = USB, left side = DIN, coloured by status, pulsing every beat
* [ ] Hold C: CSV lines (`input,tick,time_us,interval_us,batched`) appear in `make log`; hold C again stops them and the normal log returns

## Other notes

>

## Logs

```
(paste `make log` output here if needed)
```
