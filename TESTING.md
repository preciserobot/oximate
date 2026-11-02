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

## 7. Drum machine (stage 5)

Headphones on the Unit MIDI (or Synth output on Speaker). The Drums page sits
between Synth and Clock.

- [ ] Drums page appears; tapping a lane chip selects it and plays its sound
- [ ] Tapping pads cycles off → on → accent (white dot) → off
- [ ] Play (A) with the internal clock: the pattern plays on the SAM2695 and DIN OUT; the playhead moves over the pads and the overview
- [ ] Pg 1/2 shows steps 17–32; a 32-step pattern plays all of them
- [ ] Mute (B) and solo (hold B) work
- [ ] Swing (Clock page) is audible on the drums
- [ ] Pattern view: tapping a slot while playing switches at the end of the current pattern; length − / + works
- [ ] Chain: on, tap a few slots, they play in order; Chain: off clears it
- [ ] Sound view: note − / + changes the lane's sound (auditions it); velocity and accent velocity
- [ ] Following an external clock (Elektron on DIN): A arms / disarms; the drums follow the master's start and stop
- [ ] Stop: no hanging notes on external gear
- [ ] Pad controller on channel 10 (or set the pad channel in the Sound view): pads matching a lane play it
- [ ] Rec on + playing: pad hits are recorded into the pattern; hard hits record accents
- [ ] Patterns survive a power cycle (stop first: saving waits until stopped)
- [ ] Synth output on Speaker: drums play on the Core2's speaker
  > Timing: does the drum timing feel tight while the screen redraws and you tap pads?

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

## 10. Chord detector (stage 21)

- [ ] The Chord page appears after Ear
- [ ] Triads and sevenths from a keyboard are named (C, Cm, Cmaj7, C7, Cm7, Cdim, Caug, Cm7b5)
- [ ] Inversions: C/E shows "1st inversion"; a bass note outside the chord shows as a slash (C/F#)
- [ ] Extended chords: C9, Cm9, C13, C7b9, Cadd9, C6/9 are named; the alternatives line shows close readings (e.g. C6 / Am7/C)
- [ ] A rolled or strummed chord doesn't flash partial names
- [ ] Two notes show the interval (or C5 for a fifth)
- [ ] Notes are listed lowest first in their pitch-class colours; the LEDs show the chord tones in the same colours, LED 9 green/yellow/red
- [ ] History shows the last 4 chords; after a few chords the key estimate makes sense
- [ ] Hold A freezes the name (cyan); hold A again releases; A clears history
- [ ] B switches to the big simple view; C switches sharps/flats
- [ ] Chips: USB only / DIN only, a single channel, drums (ch 10) ignored by default
- [ ] Arpeggio window: an arpeggiated chord from the Oxi or M8 is named as one chord (Arp 500 / Arp bar)
- [ ] Sustain on: notes held by the pedal count
- [ ] Unplugging the USB keyboard while holding notes clears them; a panic clears them
- [ ] Settings survive a power cycle

## 11. Haptic cues (stage 10)

Cues are **off by default**: turn them on with the top chip of the Clock page's Cues view, or hold C on the Clock page.

- [ ] After a fresh start nothing vibrates or clicks; hold C on the Clock page shows "Cues ON" and the metronome starts vibrating; hold C again stops it at once

Clock page, C until the Cues view. Default: metronome vibrates.

- [ ] Start the internal clock: the motor taps on every beat, longer on beat 1
  > Does the tap feel on the beat, early, or late? (Lead setting on the Cues view)
- [ ] Cues view, B (Test): every pattern plays once; they feel different from each other
- [ ] Metronome set to Click: a click on the speaker instead; Both: tap and click
- [ ] Str: lower strength is softer; at 110 still noticeable?
- [ ] Count-in 1 bar: Start gives 4 taps (last one longer), then the clock starts on the next beat
- [ ] Following the Elektron's clock: unplugging it gives one long buzz (clock lost); plugging it back a quick double (locked again)
- [ ] Feel the kick on (channel 10, note 36): kicks from a drum machine are felt; snares aren't
- [ ] Touch tick on: a light tick on every button press and tap
- [ ] Panic (power button): a long pulse when it's done
- [ ] Ear trainer: right answer one tap, wrong answer three
- [ ] Settings survive a power cycle
- [ ] Screen redraws (e.g. Monitor while playing) don't make the metronome stumble

## 12. Generative sequencers (stage 12)

Clock running (Clock page), Elektron or headphones on the SAM2695. Slots start muted.

- [ ] Gen page appears after Drums; 8 slots, 1–4 preset (two Euclid, Turing, Mod), 5–8 off
- [ ] Unmute slot 1 (tap its chip or B): four on the floor on channel 10 (SAM2695 drum kit)
- [ ] Slot 2 hats (7 over 16) play against it; the list preview shows the playhead moving
- [ ] Edit a Euclid slot (tap its preview): steps, hits, rotation change the rhythm live
- [ ] Two Euclid slots with different step counts drift against each other and realign (hold A, next bar)
- [ ] Turing slot (channel 3 → a synth or the SAM2695): locked repeats exactly; unlocked (B) slowly changes; hold B reseeds
- [ ] Turing notes stay in the chosen scale and range
- [ ] Mod lane (channel 3, CC 74) moves a filter smoothly; S&H steps; Min/Max limit it
- [ ] Rates 1/8, 1/4, 1/32, 1/16T sound right; 1/16 follows the Clock page's swing
- [ ] Stop: no hanging notes; Start: every slot begins at step 1
- [ ] Drums page, Pattern view: Euclid fill writes hits into the selected lane
- [ ] LEDs show the Euclid ring / Turing note / mod value of the selected slot
- [ ] Settings survive a power cycle
  > Diag page DIN drops with all slots busy plus drums:

## 13. Phrase tracker (stage 22)

The Phrase page sits after Analyse. Defaults: 16-bar phrases, 4 phrases per
section, 1-bar warning.

- [ ] Internal clock: Start on the Clock page, then the Phrase page counts 1 → 16 in time; bar 1 of each phrase flashes the LEDs white
- [ ] Bar 16 (the warning bar) turns the counter and the LEDs orange
- [ ] "phrase n / 4" steps on every 16 bars and wraps after 4
- [ ] Length chip (4/8/16/32) changes the count at once without jumping strangely
- [ ] Following the Elektron (Clock source DIN) with its Start: counts from its bar 1
- [ ] Plugging into an Elektron that is already playing (no Start seen): the count runs with a red `?`; tapping B on a downbeat makes that bar bar 1 and clears the `?`
  > Is the "nearest bar" rounding forgiving enough for a slightly late or early tap?
- [ ] Hold B mid-section restarts the phrase but keeps "phrase n / 4"
- [ ] Stop on the master freezes the count; Continue carries on; Start goes back to bar 1
- [ ] Song Position from the master (if it sends it) realigns; with "SPP off" it doesn't
- [ ] C: big view is readable from a distance
- [ ] Settings survive a power cycle (changed while stopped)
  > Which phrase lengths and warning time feel right in practice?

## 15. Looper (stage 13)

Internal clock running (Clock page: Start), a keyboard on USB or DIN. The SD card in.

- [ ] Loop page appears after Phrase; LEDs show the loop state while it is open
- [ ] A arms (ARMED, red pulse); recording starts on the next bar line (REC)
- [ ] After 4 bars (default) it loops: what you played comes back in time (LOOP, green)
- [ ] A note held over the loop end doesn't hang (it is cut at the end)
- [ ] C overdubs a second pass on top; it is heard from the next cycle on; C again ends it
- [ ] Hold A (or the Undo chip) removes the last pass; Clear empties the loop
- [ ] B mutes / unmutes playback; Stop on the Clock page stops it with no hanging notes
- [ ] Bars, quantise (1/16 pulls notes to the grid), input, channel and output chips work
- [ ] "Phrase" chip: recording waits for the next phrase of the Phrase page
- [ ] Save: "Saved loop001.mid"; the file opens in a DAW on the Mac at the right tempo
- [ ] Load: the file list shows it; tapping it loads the loop, which starts at the next bar
- [ ] Saving while playing: no audible hiccup in USB/DIN timing
  > Diag system view "bus held" during a save:
- [ ] Without an SD card: Save/Load show "No SD card", looping still works

## Other notes

>

## Logs

```
(paste `make log` output here if needed)
```
