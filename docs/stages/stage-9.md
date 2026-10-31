# Stage 9: Clock analyser

> **Status: plan, not implemented.** Written 2026-10-31. Where this plan and the README's
> [Shared infrastructure](../../README.md#shared-infrastructure) section disagree, the README wins:
>
> * `MidiMessage::timeMicros` is part of the shared message type.
> * The analyser is an input observer.
> * LED bar → use the shared LED claim API.

Measures how good an incoming MIDI clock really is: tempo, jitter, drift,
dropouts and transport, per input. It can also compare the USB and DIN clocks
when both are running. Useful for checking M8 or Oxi One sync, cables, and
whether a device sends a stable clock under load.

## Measurements

Each input (USB, DIN) gets its own `ClockStats`. Statistics run over two
windows: **since reset** and **rolling** (the last 96 ticks, i.e. 4 beats).

| Metric | How |
|--------|-----|
| Tick interval min / avg / max | Δt between consecutive F8 ticks (µs) |
| Jitter (standard deviation) | Welford running variance of Δt; the rolling window uses a ring of 96 intervals |
| Max deviation | largest \|Δt − rolling mean\| |
| BPM now / averaged | 60e6 / (24 × Δt) per tick, smoothed over 24 and 96 ticks |
| Drift | BPM per beat (every 24 ticks) kept for the last 300 beats; slope in BPM/min, and the difference from the session's average BPM |
| Dropouts | gap > 3× the rolling mean interval while ticks are flowing; counted, with the longest gap |
| Transport | count and time of Start / Continue / Stop / Song Position (F2), plus the delay from Start to the first tick |
| Ticks per run | ticks between Start and Stop. Checks SPP + Continue against the tick count (1 SPP unit = 6 ticks) |
| Double clock | bimodal intervals (alternating short/long, e.g. two merged clocks) are flagged; ties in with the Router's clock-source setting |
| Batched ticks | USB only: more than one F8 in a single USB transfer, so the real spacing is unknown. These intervals are counted but excluded from the jitter statistics |

**USB vs DIN phase** (only when both run within 1% of the same BPM):

* For each DIN tick, find the nearest USB tick. Phase = t_DIN − t_USB,
  wrapped to ±½ interval.
* Show mean, standard deviation and a trend, which reveals constant latency
  between two devices and whether they slip.
* A DIN timestamp is taken at the end of the byte, so 320 µs (one byte at
  31250 baud) is subtracted before comparing.

**Memory** (internal RAM is fine): per input, a 1024-entry `uint32_t`
interval ring (4 KB), 64 histogram bins, and 300 beat BPM values as `float`.
That's about 6 KB per input.

## Timestamp accuracy

Today, ticks are timestamped in `ClockTracker::track()`, inside the
`onMidi()` callback, after `loop()` has polled the inputs. The measured
jitter is therefore mostly **our own loop jitter**:

| Path | What adds error today | Expected error |
|------|-----------------------|----------------|
| DIN | The UART itself is fine: at 31250 baud Arduino-ESP32 sets the RX FIFO-full threshold to **1 byte** (byte-by-byte into the ring buffer), and a byte takes 320 µs. The problem is that bytes wait in the ring buffer until `DinMidi::poll()` runs | **0 to the `loop()` period**: a Monitor redraw pushes a 61 KB 8-bit sprite over 40 MHz SPI (≈ 12 ms), plus USB transfers. **Up to ~15 ms** |
| USB | The host only learns about a packet when `RecvData()` runs. Full-speed USB already quantises to 1 ms frames, the device may batch several events into one packet, and all events in a transfer get the same timestamp | **1 ms + the `loop()` period**, so also **up to ~15 ms** |

At 120 BPM a tick is 20.8 ms apart, so today's numbers are only good for BPM
and dropouts, not jitter. The page shows the current **timestamp
resolution** (the maximum `loop()`/poll period measured) so you know how far
to trust it.

**Improvements** (milestone M2):

1. **Timestamp at the source.** Add `uint32_t timeMicros` to `MidiMessage`,
   so every consumer, not just the analyser, gets the arrival time.
2. **DIN: per-byte timestamps.**
   * Register `Serial2.onReceive(cb, false)` before `begin()`. At ≤ 57600
     baud the FIFO-full threshold stays at 1, so the callback runs for every
     byte.
   * The callback runs in Arduino's `uart_event_task` (priority
     `configMAX_PRIORITIES − 1`). It reads the bytes and pushes
     `(byte, esp_timer_get_time())` into a lock-free single-producer
     ring buffer.
   * `DinMidi::poll()` then parses from that ring and stamps each message
     with the time of its status byte. Realtime bytes keep their own stamp.
   * Expected error: **≈ 320 µs constant** (one byte) + **< 50 µs** callback
     latency. This works even before stage 8, because the stamp no longer
     depends on when `loop()` runs.
3. **USB: poll fast and steadily.** Move USB polling into stage 8's MIDI
   timing task at ≥ 1 kHz. Stamp each transfer with `esp_timer_get_time()`
   and mark messages from multi-event transfers as `batched`. Expected error:
   **≈ ±1 ms** (the USB frame), which is the floor for any full-speed
   device.
4. **Ground truth** (test only, optional): a GPIO edge interrupt on the DIN RX
   pad (G13, readable through the GPIO matrix alongside the UART) gives the
   exact start-bit time. Use it to validate (2) with a logic analyser.

**What stage 9 needs from stage 8** (stage 8 owns `src/clock_engine.*` and
the timing task):

* USB polling inside the timing task at ≥ 1 kHz, never blocked for more than
  ~1 ms by the screen. That means chunked sprite pushes or an SPI bus lock
  that yields between chunks.
* `MidiMessage::timeMicros` filled in before `onMidi()` is called, plus
  `batched` for USB.
* Realtime events (F8, FA, FB, FC, F2) delivered to the analyser through an
  SPSC queue, or via the timing task calling `ClockStats::onTick()` directly.
  Statistics update in that task; the UI only reads snapshots, copied under a
  short critical section.
* The internal clock's own tick times. In a loopback test (DIN OUT → DIN IN)
  this measures our measurement floor.

## Code

| File | Purpose |
|------|---------|
| `src/clock_stats.h/.cpp` | `ClockStats`: per-input interval ring, Welford statistics, histogram, beat-BPM history, dropout/double-clock/batch detection, transport counters, `snapshot()` for the UI. Pure C++ with no M5 dependencies, so it can be tested on the Mac |
| `src/clock_phase.h` | `ClockPhase`: USB-vs-DIN phase from two tick streams |
| `src/analyser_page.h/.cpp` | `AnalyserPage : Page`: views, sprite graphs, LED status, CSV streaming |
| changed: `src/midi_message.h` | add `timeMicros` and `batched` |
| changed: `src/din_midi.*` | `onReceive` byte timestamps (M2) |
| changed: `src/usb_midi.*` | per-transfer stamp and batched flag (M2) |
| changed: `src/clock_tracker.h` | uses `timeMicros` instead of `micros()`; the header keeps using it |
| changed: `src/leds.*` | the override API, if stage 8 hasn't added it already |
| changed: `src/ui.*` | `Page::onButtonHold(int)`, for CSV export |

* `onMidi()` calls `analyser.track(msg)` for realtime messages, before
  routing, like `clockTracker`. Stats always run in the background, so the
  numbers are ready when the page is opened.
* Timestamps use `uint32_t` µs with unsigned subtraction, which is
  wrap-safe. `micros()` wraps after 71 minutes, and interval maths survives
  that.

## Page UI

The page is titled `Analyse`. **A** resets the statistics, **B** cycles the
view (Stats → Histogram → Drift → Phase), and **C** picks which input the
graph views show (USB/DIN). **Holding C** toggles CSV streaming. Tap the
header for the next page, as on every page.

Stats view: both inputs side by side, text only, redrawn at most 4× per
second.

```
┌────────────────────────────────────────┐
│Analyse> USB 1C75:0288   DIN 120.0 PLAY │
│              USB          DIN          │
│BPM now       --           120.03       │
│BPM avg 4b    --           120.00       │
│Interval      --           20.833 ms    │
│ min / max    --           20.51/21.20  │
│Jitter sd     --           0.142 ms     │
│Max dev       --           +0.37 ms     │
│Dropouts      --           0 (max 0ms)  │
│Ticks/run     --           1536         │
│Transport     --           PLAY SPP 32  │
│Phase D-U     n/a  (one clock only)     │
│Res ±0.4ms  Status --      STABLE       │
│   Reset      View:Stats   Graph:DIN    │
└────────────────────────────────────────┘
```

Histogram view: an M5Canvas sprite, 320×192 at 8-bit (61 KB, the same size
as the Monitor's). Deviation from the mean on the x axis, auto-ranged to
±1/2/5/10 ms; count on the y axis. A bimodal shape shows a double clock at a
glance.

```
┌────────────────────────────────────────┐
│Analyse> USB --          DIN 120.0 PLAY │
│DIN jitter  n=1536  sd 0.14ms  ±1ms     │
│                   █                    │
│                  ███                   │
│                 █████                  │
│               ▁███████▁                │
│            ▁▂▅█████████▅▂▁             │
│ -1ms            0              +1ms    │
│   Reset      View:Hist    Graph:DIN    │
└────────────────────────────────────────┘
```

* **Drift view**: BPM per beat for the last 300 beats as a line. y is
  auto-ranged around the mean (minimum ±0.5 BPM); dropouts are red
  markers; the slope is shown as `+0.02 BPM/min`.
* **Phase view**: DIN − USB offset per beat over time, with mean and
  standard deviation. Shows "need both clocks" when one is missing.
* Graphs redraw at most 10× per second, and only while the page is visible.

## LED bar

While the Analyse page is visible it takes over the LEDs: the USB side
(LEDs 0–4) and the DIN side (LEDs 5–9) each show their input's status. Each
side pulses brighter on every beat (every 24th tick), so you can see the
tempo too. On other pages the normal activity display returns.

| Colour | Condition (rolling window of 96 ticks) |
|--------|----------------------------------------|
| off | no clock for 2 s |
| green | jitter sd < 0.5 ms **and** max deviation < 1.5 ms |
| yellow | sd < 2 ms, or a double clock suspected |
| red | dropout in the last 2 s, or sd ≥ 2 ms |

The thresholds are meaningful once timestamps are accurate (M2). Before
that, the status is capped at yellow when the timestamp resolution is
worse than 1 ms, so that our own loop jitter doesn't show up as red.

This needs a small override API in `Leds` (e.g. `setOverride(bool)` and
`setPixel(i, color)`), as already planned in the README. Whichever of
stages 8 and 9 comes first adds it.

## CSV export (optional)

* **Serial** (M5): holding C toggles streaming of one line per tick at
  115200 baud. Format: `input,tick,time_us,interval_us,deviation_us,batched`.
  At 120 BPM with both clocks that's about 2 KB/s, well under the ~11 KB/s
  the serial port can carry. Monitor lines are suppressed while streaming,
  so the CSV stays clean. A small Python script can plot it on the Mac.
* **SD**: later, with stage 13's SD writer. Ticks are buffered in PSRAM and
  written in 4 KB chunks, because the SD card shares the SPI bus with the
  screen and USB.

## Milestones

1. **M1: statistics core + Stats view.** `ClockStats` and `ClockPhase` with
   Mac unit tests, the Analyse page Stats view, and the displayed timestamp
   resolution. Uses the current timestamps.
2. **M2: accurate timestamps.** `MidiMessage::timeMicros` and `batched`, DIN
   `onReceive` byte stamps, and USB stamps. Full USB accuracy comes with
   stage 8's timing task.
3. **M3: graph views.** Histogram, Drift and Phase sprites.
4. **M4: LED status.** `Leds` override API, thresholds, beat pulse.
5. **M5: CSV over serial.** SD export comes later, with stage 13.

## Test plan

* **Unit tests on the Mac** (stub headers, same approach as stages 1 and 2):
  * a perfect 120 BPM stream gives sd = 0 and BPM = 120.000
  * Gaussian jitter of 0.5 ms is measured as 0.5 ± 0.05 ms
  * a 100 ms gap is counted as one dropout
  * an alternating 10/31 ms pattern is flagged as a double clock
  * two streams 3 ms apart give a phase of 3.0 ms
  * `micros()` wrap-around in the middle of a stream is handled
  * batched ticks are excluded from the jitter statistics
* **Measurement floor (loopback).** Cable DIN OUT to DIN IN and send a
  timer-driven clock: stage 8's internal clock, or a temporary `esp_timer`
  test that writes F8. After M2, expect sd < 0.1 ms on DIN.
* **Ground truth.** Put a logic analyser on G13, the opto output, and
  compare its byte start times with the logged CSV. Our offset should stay
  constant (≈ 320 µs) with spread under 0.1 ms.
* **Known external clock.** Measure the M8 and the Oxi One at fixed tempos,
  then a computer DAW (expect worse) and a USB keyboard's clock out, if it
  has one.
* **Under load.** Send dense note streams on DIN while clock runs, and
  check the clock stats still read the source's real jitter. Then toggle
  between the Monitor and Analyse pages to confirm screen redraws don't
  change the numbers after M2.
* **Transport.** Start/Stop/Continue with SPP from the M8 gives correct
  counts, Start-to-first-tick delay and ticks-per-run consistency.

## Risks and open questions

* **Monitor redraw blocking USB.** Until stage 8 moves polling into its own
  task, USB jitter figures are dominated by our loop. They're labelled as
  such on screen.
* **`onReceive` per byte.** The callback is assumed to fire per byte when
  the FIFO-full threshold is 1, without extra delay from the RX timeout.
  Verify on hardware. If it doesn't, use the ESP-IDF UART event queue
  directly, or the GPIO edge interrupt.
* **USB batching.** Some devices batch several MIDI events into one USB
  packet every 1 ms or more. Their true jitter can't be seen from the host;
  we can only report "batched" and frame-level timing.
* **Crystal reference.** Our reference is the ESP32's crystal (about
  ±10–40 ppm), so drift below about 0.005 BPM at 120 BPM isn't meaningful.
* **Ownership of the LEDs and the second core.** Coordinated with stage 8.
  The analyser must not add work to the timing task beyond O(1) per tick.
* **Open questions for the user:**
  * Which clock sources matter most to test (M8, Oxi One, DAW)?
  * Is CSV over serial enough, or should SD logging happen in this stage?
  * Should the analyser also measure clock **sent** by OxiMate (stage 8),
    for example by showing the loopback result in the Phase view?
