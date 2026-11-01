# Stage 16: Panic & diagnostics

> **Status: plan, not implemented.** Written 2026-10-31. Where this plan and the README's
> [Shared infrastructure](../../README.md#shared-infrastructure) section disagree, the README wins:
>
> * `MidiOutputs` and `NoteTracker` are shared infrastructure, built before the first stage that generates MIDI.
> * LED warning overlays sit above any LED claim (see the shared LED claim API).
>
> **Decided 2026-11-01:**
>
> * The global trigger is the **power button**, as recommended.
> * Panic also sends **Stop** (`FC`).
> * ~~Stuck notes are released automatically after 30 s~~ — removed after hardware testing (2026-11-01): long pads are held on purpose, so notes are never cut on a timer.

Get rid of stuck notes quickly, prevent them from happening, and show what
OxiMate is doing. This stage also defines the **note tracker** and a single
**output layer** that every later stage sends MIDI through.

## Goals

* **Panic** from any page, in three levels of force.
* **Prevent stuck notes**: when a source disappears or the router settings
  change while notes are held, OxiMate sends the matching note-offs itself.
* **Stuck-note detector**: warn about notes held longer than a threshold.
* **Diagnostics page**: per-channel activity, message counters and rates,
  buffer and error counters, and system health (memory, battery, loop time).
* **LED bar**: per-channel activity on the Diagnostics page, and warning
  flashes on every page.

## Architecture: one output layer

Today `Router::route()` calls `DinMidi::send()` and `UsbMidi::send()`
directly. Later stages (drum machine, synth, ear trainer, Push 2) will also
send MIDI. To track notes and count traffic correctly, **everything sent goes
through one choke point**:

```
 inputs ─► onMidi() ─► Router ─┐
 stage 5/7 generators ─────────┼─► MidiOutputs::send(out, msg, origin)
 stage 6 SamSynth ─────────────┤        ├─► DinMidi / UsbMidi
 stage 4 Push 2 sequencer ─────┘        ├─► NoteTracker (what was SENT)
                                        └─► TX counters
```

* New `src/midi_outputs.*`: `send(MidiOutput out, const MidiMessage &msg,
  Origin origin)`, `sendRaw(out, bytes, len)` for panic streams, TX counters.
* `Router` sends through `MidiOutputs` and passes the message's input as the
  origin. This tracks notes **after** remapping: a note forced to channel 10
  is tracked on channel 10.
* **Origin** says who caused a note: `UsbIn`, `DinIn`, or `Internal(owner)`.
  The owner is a small id per generator (for example 5 = drum machine,
  7 = ear trainer), so a mode can release only its own notes when it stops.
* **Threading** (with stage 8): stage 8 moves MIDI I/O into a timing task on
  the second core. `MidiOutputs`, `NoteTracker` and the panic job then run
  inside that task. The UI only **requests** actions (an atomic flag or a
  queue) and reads **snapshots** taken under a `portMUX` spinlock. Until
  stage 8 lands, everything runs in `loop()` and the lock costs nothing.

## Note tracker (`src/note_tracker.*`)

Tracks held notes per **output** (DIN, USB) and channel, based on what was
actually sent.

| Rule | Behaviour |
|------|-----------|
| Note-on, velocity > 0 | note becomes held |
| Note-off, or note-on with velocity 0 | note released |
| Same note-on twice | still one held note; a single note-off releases it (most synths behave this way) |
| CC64 ≥ 64 (sustain down) | released notes on that channel move to "sustained" |
| CC64 < 64 (sustain up) | sustained notes on that channel are cleared |
| CC123 / CC120 sent | everything on that channel is cleared |

Memory per output: `held` and `sustained` bitsets (16 × 128 bits = 256 bytes
each), a 16-bit sustain-pedal mask, and a 128-slot table of
`{channel, note, origin, startMillis}` (8 bytes per slot, 1 KB) for ages and
origins. That's about **1.6 KB per output, 3.2 KB in total**. If more than 128
notes are held, the bitsets stay correct and only age and origin are lost for
the extra notes.

```cpp
enum class MidiOutput : uint8_t { Din = 0, Usb = 1 };

struct Origin
{
  enum Kind : uint8_t { UsbIn, DinIn, Internal } kind;
  uint8_t owner; // stage id for Internal, 0 otherwise
};

class NoteTracker
{
public:
  void sent(MidiOutput out, const MidiMessage &msg, Origin origin); // called by MidiOutputs only

  bool isHeld(MidiOutput out, uint8_t channel, uint8_t note) const;
  uint16_t heldCount(MidiOutput out) const;   // held + sustained
  uint16_t sustainMask(MidiOutput out) const; // channels with the pedal down

  /*! Visit held notes, optionally only those from one origin */
  template <typename F> void forEachHeld(MidiOutput out, F visit, const Origin *only = nullptr) const;

  /*! Send note-offs (and CC64=0 where the pedal is down) through MidiOutputs, then forget them */
  void release(MidiOutputs &outputs, MidiOutput out, const Origin *only = nullptr);

  void clear(MidiOutput out); // forget without sending (e.g. the USB device is gone)

  /*! Oldest held note older than thresholdMillis, for the stuck-note warning */
  bool findStuck(uint32_t thresholdMillis, MidiOutput &out, uint8_t &channel, uint8_t &note,
                 uint32_t &ageMillis) const;
};
```

Users of the tracker:
* **Stage 6** (`SamSynth`): an all-notes-off for the synth calls
  `release(Din, &synthOrigin)`.
* **Stage 5** (drum machine) and **stage 4** (Push 2 sequencer): on stop or
  pattern change, they call `release(…, &ownOrigin)`.
* **Stage 7** (ear trainer): calls `release` when it leaves the page.

## Panic levels

| Level | What is sent (per output: DIN, USB) | DIN bytes | DIN time |
|-------|-------------------------------------|-----------|----------|
| 1 Release | note-off for every tracked held or sustained note, plus CC64=0 where the pedal is down | 3 per note | ~1 ms per note |
| 2 All off | level 1, then CC123 (All Notes Off), CC120 (All Sound Off) and CC64=0 on all 16 channels | +144 | +46 ms |
| 3 Brute force | level 2, then note-off for all 128 notes on all 16 channels, using running status | +4112 | +1.3 s |

* DIN runs at 31250 baud, about 3125 bytes per second. Without running status,
  level 3 would be 6144 bytes (2.0 s). With running status, each channel's
  status byte is sent once followed by 128 × 2 data bytes, giving
  16 × 257 = 4112 bytes.
* Level 3 is bigger than the 1 KB DIN transmit buffer, so a non-blocking
  **panic job** (`src/panic.*`) feeds it in chunks with
  `Serial2.availableForWrite()`. MIDI input keeps flowing, and the job uses
  `DinMidi::sendRaw()` to bypass message framing. Normal output to DIN pauses
  until the job finishes, so other messages can't break the running status.
* On USB, the job batches 16 USB-MIDI events (64 bytes) per transfer with
  `SendRawData`, so level 3 needs 128 transfers instead of 2048.
* The SAM2695 is on the same wire as DIN OUT, so panic silences it as well.
* After any panic, the tracker for that output is cleared.

**Trigger** (works on every page and doesn't take any A/B/C button from the
current page):

* **Recommended: the power button** (left side of the Core2), read through
  `M5.BtnPWR`. On the Core2 it's read from the AXP192 power chip, which reports
  short and long presses; M5Unified polls it every few milliseconds.
  * **Short press**: levels 1 + 2. The header shows `PANIC` for one second.
  * **Long press (about 1.5 s)**: level 3. The LED bar shows progress.
  * Caveat: holding it for **6 s** powers the Core2 off in hardware. The
    level 3 hold is well below that, but don't hold it for long.
* Fallback: hold the header for 2 seconds to trigger levels 1 + 2. A normal
  tap still switches pages, because M5Unified reports a hold differently from
  a click.
* On the Diagnostics page, **C** gives the same choice: click for levels 1 + 2,
  hold for level 3.

## Avoiding stuck notes automatically

| Event | Action |
|-------|--------|
| USB device unplugged (`USBH_MIDI::attachOnRelease`) | `clear(Usb)` (the device is gone); `release(Din, origin UsbIn)` (its notes on DIN) |
| DIN input stops sending Active Sensing (FE) for more than 300 ms, after sending it before | `release(all outputs, origin DinIn)`. This is what the MIDI spec says a receiver should do. |
| Router setting changed (route off, channel blocked, forced channel changed) while notes are held | `release(…, origin of that input)` **before** applying the new setting, so the note-offs still reach the same output and channel as the note-ons |
| A mode stops or leaves its page (drum machine, sequencer, ear trainer) | that mode calls `release(…, its own origin)` |

The router change is the easiest to miss. Without it, holding a note while
changing `USB>keep` to `USB>ch5` would send the note-off to channel 5 and leave
the original note stuck.

## Stuck-note detector

* Each second, `findStuck()` reports the oldest note held longer than the
  threshold (default **30 s**, adjustable; 0 turns it off).
* Default is **warn only**: the LED bar flashes yellow and the Diagnostics
  page lists the note. Drones and pads are often held on purpose.
* An optional **auto-release** setting sends the note-off when the threshold
  is reached.

## Diagnostics page

The page has three views, selected with **B**.

```
┌────────────────────────────────────────┐
│Diag>    USB 1C75:0288   DIN 120.0 PLAY │
│ ch  1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6    │
│ USB # # . . . . . . . . . . . . . .    │
│ DIN . . . . . . . . . # . . . . . .    │
│ Out held   DIN 3   USB 0               │
│ Sustain    DIN ch1                     │
│ Stuck      DIN ch10 D2 held 41 s       │
│                                        │
│                                        │
│   Reset        View:ch      Panic      │
└────────────────────────────────────────┘
```

* **Channels view**: `#` = activity within the last 250 ms (cyan for USB,
  orange for DIN), `.` = idle. It also shows held-note counts per output,
  channels with the sustain pedal down, and the oldest stuck note.

```
┌────────────────────────────────────────┐
│Diag>    USB 1C75:0288   DIN 120.0 PLAY │
│              USB in   DIN in    /s     │
│ Note on/off   1234      560     12     │
│ CC             880       14      3     │
│ Bend/AT/PC      40        0      0     │
│ Clock/transp  9216     9216     48     │
│ SysEx ok/drop  3/0      0/1      0     │
│ Filtered        12        0      0     │
│ Out DIN/USB   2310 / 570 msgs          │
│                                        │
│   Reset        View:cnt     Panic      │
└────────────────────────────────────────┘
```

* **Counters view**: totals per input since the last reset, with a 1-second
  rate. "Filtered" counts messages the router dropped (channel filter, clock
  source, SysEx off).

```
┌────────────────────────────────────────┐
│Diag>    USB 1C75:0288   DIN 120.0 PLAY │
│ Uptime    01:23:45                     │
│ Heap      212 KB free (min 198 KB)     │
│ PSRAM     4093 KB free                 │
│ Battery   87%  4.05 V  charging        │
│ DIN TX    buf 12/1024 peak 640 stall 0 │
│ DIN RX    overrun 0  stray 0           │
│ USB       err 0  rcode 00  reconn 1    │
│ Loop      avg 1 ms  max 18 ms          │
│                                        │
│   Reset        View:sys     Panic      │
└────────────────────────────────────────┘
```

* **System view**:
  * memory: `ESP.getFreeHeap()`, `getMinFreeHeap()`, `ESP.getFreePsram()`
  * battery: `M5.Power.getBatteryLevel()`, `getBatteryVoltage()`,
    `isCharging()`
  * transport counters (below)
  * main-loop time: average and maximum. This shows when screen redraws block
    MIDI, which is the data stage 8 needs.
* Buttons: **A** = reset counters (not the tracker). **B** = next view.
  **C** = panic (click: levels 1 + 2, hold: level 3).
* The page redraws at most 4 times per second, using the existing `Ui`
  throttling.

## Counters to add

| Module | New counters |
|--------|--------------|
| `DinMidi` | `rxBytes`, `rxMessages`, `rxSysExDropped` (overflow or interrupted by a status byte), `rxStrayData` (data byte without status), `rxOverruns` (via `Serial2.onReceiveError()`: buffer-full / FIFO overflow), `txBytes`, `txPeak` (`1024 - availableForWrite()` high-water mark), `txStalls` (a send found the buffer full and blocked) |
| `UsbMidi` | `rxPackets`, `rxMessages`, `sysexDropped` (overflow or orphan continuation), `recvErrors` (`RecvData` return code other than 0 or `hrNAK`), `sendErrors`, `lastRcode`, `connects`, `disconnects` |
| `Router` | `filtered[reason]` (channel, clock, SysEx), `forwarded` |
| `Stats` (`src/diagnostics.*`) | per input: count per message type (note, CC, bend/AT/PC, clock/transport, SysEx, other), last-seen time per channel (`uint32_t [2][16]`); main-loop time average and maximum |

All counters are `uint32_t` and are written only from the MIDI side (`loop()`
now, the stage 8 task later). The page reads a snapshot copy.

## LED bar

* **While the Diagnostics page is shown**: LEDs 0–9 show activity on channels
  1–10 (cyan = USB, orange = DIN, white = both, fading over about 150 ms). This
  needs a `Leds` mode switch: `Activity` (default) / `Channels` / `External`
  (owned by a page).
* **Warnings on every page**: a short overlay on top of whatever the LEDs are
  showing.
  * Red, 3 blinks: SysEx dropped, DIN RX overrun, or a USB error.
  * Yellow, 1 blink every 5 s: a stuck note.
  * White flash: panic sent.
  * Level 3 panic: a white progress bar filling over about 1.3 s.
* New `Leds` API: `setMode(mode)`, `warn(color, blinks)`, `progress(fraction)`,
  `setPixel(index, color)` (shared with later stages).

## Settings (NVS)

Settings use their own versioned struct (`DiagConfig`) under the key `diag` in
the existing `oximate` namespace:
* stuck threshold in seconds (default 30)
* stuck auto-release (default off)
* panic sends Stop (`FC`) as well (default off)
* panic sends to USB (default on)

## New and changed files

| File | Change |
|------|--------|
| `src/note_tracker.*` | new: held/sustained tracking, origins, release, stuck detection |
| `src/midi_outputs.*` | new: the single send path; calls the tracker; TX counters; `sendRaw` |
| `src/panic.*` | new: non-blocking panic job (levels 1–3), chunked DIN and batched USB output |
| `src/diagnostics.*` | new: `Stats` counters and snapshot, loop timing |
| `src/diag_page.*` | new: the Diagnostics page (3 views) |
| `src/din_midi.*` | counters, `sendRaw()`, `availableForWrite()`, `onReceiveError` hook |
| `src/usb_midi.*` | counters, disconnect callback (`attachOnRelease`), raw batched send |
| `src/router.*` | send through `MidiOutputs` with origin; filter-reason counters; release held notes before a setting change takes effect |
| `src/leds.*` | modes, warning overlay, progress, `setPixel` |
| `src/ui.*` | header hold → panic; `PANIC` message in the header |
| `oximate.ino` | `M5.BtnPWR` → panic; wire in the new modules; loop timing |

## Milestones

1. `NoteTracker` and `MidiOutputs`, with the router switched over. Host unit
   tests (extend the existing stub harness): note-on/off, velocity 0, double
   note-on, sustain, remapped channels, slot overflow.
2. Panic levels 1–3 as a non-blocking job. Triggers: power button, header
   hold, and Diagnostics C. Header message and LED feedback.
3. Automatic release on USB disconnect, Active Sensing timeout and router
   setting changes.
4. Counters in `DinMidi`, `UsbMidi` and `Router`, plus `Stats`. Diagnostics
   page with all 3 views.
5. LED channel mode and warnings; stuck-note detector; `DiagConfig` in NVS.
   Update the README and commit.

## Test plan

* **Host tests**:
  * tracker rules (the table above)
  * `release()` produces exactly the expected note-offs
  * level 3 produces 4112 DIN bytes with running status
  * the panic job never writes more than `availableForWrite()` returns
* **On hardware**:
  * Hold a key on a USB keyboard, then unplug it: the SAM2695 note stops.
  * Hold a key and change `USB>keep` to `USB>ch5`: no hanging note.
  * Press sustain, play, then press the power button: silence, and the pedal
    state is reset.
  * Short and long power-button presses: confirm panic triggers, the device
    does not power off, and the trigger feels responsive.
  * Use a DIN keyboard that sends Active Sensing, hold a note and pull the DIN
    cable: the note is released within about 300 ms.
  * Flood USB with pitch bend going out to DIN: the TX peak and stall
    counters rise and the loop maximum shows the blocking.
  * Time a level 3 panic: expect about 1.3 s on DIN, and MIDI input keeps
    working during it.

## Risks and open questions

* **Power button as the panic trigger.** It's always reachable and doesn't
  take a page button, but a 6-second hold powers the device off. Is that
  acceptable, or should level 3 only be available from the Diagnostics page?
* **Stuck-note threshold.** 30 s, warn only, may be too short for drones and
  too long for live use. What fits your workflow?
* **Should panic also send Stop (`FC`)?** That would halt the M8 or other
  sequencers. The default is off.
* **Should panic go to the USB device?** That's useful when the M8 is on USB,
  but pointless for a keyboard. The default is on.
* **Synths that ignore CC123 or CC120.** This is why level 3 exists. Some old
  gear also treats note-on velocity 0 differently from note-off, so level 3
  uses real note-off (`8n`) messages.
* **Locking and threading.** These depend on how stage 8 sets up the timing
  task (which core, queue design). This plan assumes all sends happen on the
  MIDI side and the UI only reads snapshots.
* **One tracker per output, not per device.** A USB hub with several devices
  would need tracking per device. That's out of scope until stage 4 or later.
