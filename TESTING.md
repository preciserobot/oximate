# Hardware test checklist

Firmware: `develop` at commit `1b96a57` (stages 1, 2, 8 and 16), 2026-11-01.

How to use this file:

* Tick a box with `[x]` when it works.
* Write `FAIL:` plus what happened on the line below an item that doesn't.
* Add notes anywhere (`>` lines are easiest to spot).
* If something freezes, resets or leaves a note stuck, run
  `make log SECONDS=30` while reproducing it and paste the output at the end.

Before starting: set the Unit MIDI's front switch to **Separate**.

## 1. Basics (must pass before moving on)

- [ ] The start-up screen (red "oximate" banner) shows for about 3 s, then the Monitor page appears
- [ ] Swiping left/right switches pages: Monitor → Router → Clock → Diag (tapping the header also works)
- [ ] Notes from a USB keyboard show on the Monitor in cyan
- [ ] Notes from DIN IN show on the Monitor in orange
- [ ] Both inputs come out of DIN OUT
- [ ] Both inputs are audible on the SAM2695 (headphones on the Unit MIDI)
- [ ] DIN IN notes reach the USB device
- [ ] Monitor buttons: A clears, B pauses/resumes, C shows/hides clock messages
- [ ] LEDs flash on incoming notes
  > Which side lights up for USB, and which for DIN:
- [ ] Playing fast on both inputs while switching pages: no missing notes, no stuck notes, no freezes

## 2. Router page

- [ ] Turning a route off (e.g. USB>DIN) stops that traffic
- [ ] A blocked channel stops passing, and its Monitor lines turn grey
- [ ] Force channel works (e.g. USB>ch5)
- [ ] Holding a note while changing force channel or blocking its channel: no stuck note
- [ ] Settings are still there after a power cycle

## 3. Clock page

- [ ] Start/Stop sends clock to the M8 over USB (the M8 follows the tempo and starts/stops)
- [ ] Outputs view: switching DIN clock on makes the Elektron follow over DIN
- [ ] The tempo chips (±1, ±0.1) and tap tempo (B) change the tempo
- [ ] The LEDs show beat and bar while the Clock page is open
- [ ] Source DIN with the Elektron sending clock: tempo shown, rightmost LED turns blue (locked)
- [ ] Stopping or unplugging the Elektron's clock: the display shows LOST and OxiMate stops
- [ ] Clock settings are still there after a power cycle

## 4. Panic and stuck notes

- [ ] Short power-button press: PANIC in the header, LEDs flash red, held notes stop
- [ ] Holding the power button ~1.5 s: PANIC ALL, the red LED bar fills over about 2 s
- [ ] Holding a note on a USB keyboard and unplugging it: the note stops on DIN
- [ ] A note held for more than 30 s is released automatically
  > Is the 30 s auto-release OK for drones/pads you hold on purpose?

## 5. Diag page

- [ ] Channel view shows activity; channels 1–10 light up on the LEDs
- [ ] Counters go up while playing; A resets them
- [ ] System view while playing heavily:
  > UI loop max:
  > USB bus busy:
  > UI events dropped:

## Other notes

>

## Logs

```
(paste `make log` output here if needed)
```
