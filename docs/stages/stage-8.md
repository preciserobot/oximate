# Stage 8: Clock master

> **Status: plan, not implemented.** Written 2026-10-31. Where this plan and the README's
> [Shared infrastructure](../../README.md#shared-infrastructure) section disagree, the README wins:
>
> * DIN realtime-priority output lives behind the shared `MidiOutputs` layer from stage 16.
> * `ClockTracker` is replaced as described here. The stage 9 analyser keeps its own statistics in `clock_stats`.
>
> **Decided 2026-11-01:**
>
> * Clock output defaults to **USB**, switchable to DIN, per output.
> * When the external clock is lost, OxiMate **stops**.

Stage 8 does two things:

* It adds the **clock engine** (`src/clock_engine.*`): an internal MIDI clock
  with tap tempo, swing, start/stop and per-output dividers. It can also follow
  an external clock.
* It sets up the **timing architecture** that the other clock-accurate stages
  depend on: a MIDI task on its own core, a hardware timer, and safe sharing of
  the SPI bus.

Stage 8 is the first stage that changes how existing modules run, so the
infrastructure milestones (M1 and M2) come first and must not change any
behaviour.

Clock engine consumers:

* stage 5: drum machine
* stage 4: Push 2 step sequencer (`src/step_sequencer.*`)
* stage 9: clock analyser
* stage 10: haptic metronome
* stage 12: generative sequencers
* stage 20: DFAM gate pulses

## Why the current loop isn't good enough

The current design does everything in Arduino's `loop()`: USB polling, DIN
reading, routing, drawing and LEDs. Several of these block for long enough to
throw a 24 PPQN clock off (a tick every 20.8 ms at 120 BPM):

| Blocker | Typical duration | Effect on timing |
|---------|------------------|------------------|
| Monitor `pushSprite` (61 KB over SPI) | ~12–15 ms | all MIDI input and output paused |
| Router page full redraw | ~5–10 ms | same |
| `HardwareSerial` TX backlog (1 KB buffer) | up to 330 ms of queued bytes | a clock byte waits behind queued notes |
| `Usb.Task()` enumeration | 100s of ms (only on plug-in) | same |

## Timing architecture

```
 core 0                                  core 1
 ┌─────────────────────────────┐         ┌──────────────────────────┐
 │ midiTask  (prio 10)         │ events  │ loopTask (Arduino, prio 1)│
 │  wait: timer notify | 1 ms  │ ──────► │  drain event ring        │
 │  clock.runDue()             │  ring   │  monitor.log, leds, IMU  │
 │  din.poll()  din.pumpTx()   │         │  ui.poll() (draws)       │
 │  usb.poll() [SPI try-lock]  │ ◄────── │  config edits (snapshot) │
 │  router.route()             │ config  │                          │
 └─────────────────────────────┘         └──────────────────────────┘
   ▲ hardware timer ISR (1 MHz, one-shot alarm) → xTaskNotifyFromISR
```

* **`midiTask`** (`src/midi_task.*`) is pinned to **core 0** at priority 10,
  with an 8 KB stack. It owns `UsbMidi`, `DinMidi`, `Router` and
  `ClockEngine`, and is the only task that calls them.
  * Core 0 is free while there's no radio. Stages 17 and 18 (BLE and Wi-Fi)
    run their stacks on core 0 at priorities 18–23, which would preempt this
    task. Those stages must re-measure timing, and may move the task to core 1
    (it outranks `loopTask` there).
  * The core is a single constant: `kMidiCore`.
* **The hardware timer** uses the Arduino 3.x API (`timerBegin(1000000)`,
  `timerAttachInterrupt`, `timerAlarm`), running free at 1 µs per count.
  * The ISR does nothing except call `vTaskNotifyGiveFromISR`.
    Scheduled callbacks then run in `midiTask` context, about 10–30 µs after
    the alarm.
  * The task also wakes every 1 ms (`ulTaskNotifyTake(pdTRUE, 1)`), which
    covers USB and DIN polling and feeds the watchdog.
* **One loop iteration of `midiTask`:**
  1. Run all due scheduled events (clock ticks, steps, note-offs).
  2. `din.poll()`: parse received bytes. Messages from the clock's followed
     input go to `ClockEngine::onExternal()`.
  3. `usb.poll()`, only if the SPI lock can be taken **without waiting**.
     Otherwise skip it and retry in 1 ms. The timing task never blocks on the
     display.
  4. `din.pumpTx()`: feed the UART from the priority queue (see below).
* **The UI side stays on core 1 in `loop()`.** It drains the event ring into
  `Monitor`, `Leds` and `ClockTracker`/header state, handles touch, the IMU and
  vibration (all on the internal I2C bus, so UI thread only), and draws.

## Thread-safe handoff

| Data | Direction | Mechanism |
|------|-----------|-----------|
| Routed messages for Monitor and LEDs | MIDI → UI | single-producer/single-consumer ring of 256 `MidiEvent` records `{uint32_t micros; source; cable; len; bytes[3]; forwarded}`. SysEx is copied as its first 16 bytes plus the total length. Overflow drops the oldest entry and increments a counter (shown by stage 16). |
| Clock state for the header and Clock page | MIDI → UI | `ClockEngine::snapshot()` copies a small struct under a `portMUX` critical section |
| Clock events for the UI (beat, bar, start/stop) | MIDI → UI | flags in the same ring (`MidiEvent` with a pseudo-source `Clock`) |
| `RouterConfig` and `ClockConfig` edits | UI → MIDI | the UI edits its own copy, then `apply()` copies it into the task's active config under `portMUX`. `route()` only reads the active copy. |
| USB connected, VID and PID | MIDI → UI | atomics updated by `midiTask` after each poll |

## SPI bus sharing (display, USB host, SD card)

The Core2's display (M5GFX, ESP-IDF SPI driver), the MAX3421E (Arduino `SPI`)
and the SD card all use the same **VSPI** peripheral through different
drivers. Today this only works because everything runs on one thread.

* **New `src/spi_bus.h`:** one global FreeRTOS mutex plus an RAII `SpiLock`
  guard. It supports `tryLock()` (used by `midiTask`) and a blocking lock
  (used by the UI and SD code).
* **`UsbMidi::poll()` and `send()`:** take the lock around
  `Usb.Task`/`RecvData`/`SendData`/`SendSysEx`. When the lock is busy, a send
  goes into a small USB TX queue that is flushed on the next poll.
* **`Ui::poll()`:** takes the lock around each `startWrite()…endWrite()`
  block.
* **`Monitor::draw()`:** pushes its sprite in **4 bands of 48 rows** (about
  3 ms each) and releases the lock between bands, so USB is never starved for
  more than about 3 ms.
* **SD card writes** (stages 13 and 14) must use the same lock and write in
  chunks of 4 KB or less.
* Check that M5GFX doesn't leave a DMA transfer running after `endWrite()`
  (it shouldn't when `pushImageDMA` isn't used).

## DIN output with realtime priority

* `HardwareSerial` with a 1 KB TX buffer can queue hundreds of milliseconds of
  data ahead of a clock byte. Change `DinMidi` to:
  * **Two software queues**: realtime (F8–FF), and everything else (2 KB).
  * **`pumpTx()`** writes to the UART only when the hardware FIFO holds 2 bytes
    or fewer (TX buffer size set to 0 so writes go straight to the FIFO, and
    `availableForWrite()` tells us how full it is). Pending realtime bytes are
    always written first.
  * MIDI allows realtime bytes in the middle of other messages, even SysEx. So
    a clock byte waits at most for the bytes already in the FIFO plus one pump
    period: **≤ ~1.6 ms worst case, typically < 0.7 ms**.
* Throughput stays about the same: the FIFO is topped up every 1 ms, and DIN
  sends a byte every 320 µs.

## Clock engine API (`src/clock_engine.h`)

```cpp
enum class ClockMode : uint8_t { Internal, FollowUsb, FollowDin };
enum class ClockOutput : uint8_t { Din, Usb, Gate /* stage 20 */, Count };

struct ClockOutputConfig {
  bool enabled = true;      // send F8 ticks
  bool transport = true;    // send FA/FB/FC/F2
  int8_t ratio = 1;         // 1 = x1, 2..4 = multiply, -2..-4 = divide
};

struct ClockConfig {
  static constexpr uint8_t kVersion = 1;
  uint8_t version = kVersion;
  ClockMode mode = ClockMode::Internal;
  float bpm = 120.0f;           // 20.0 .. 300.0, 0.1 steps
  uint8_t swing = 50;           // 50 = straight .. 75; 67 ≈ triplet feel
  uint8_t swingGrid = 16;       // swing 8ths or 16ths
  uint8_t beatsPerBar = 4;
  bool imuTap = false;          // tap the case for tap tempo
  ClockOutputConfig outputs[(int)ClockOutput::Count];
};

struct ClockEvent {
  enum Type : uint8_t { Tick, Step, Beat, Bar, Start, Stop, Continue, Locate } type;
  uint32_t tick;     // 24 PPQN ticks since start (or song position)
  uint32_t step;     // 16th-note index (Step events, swing applied)
  uint64_t timeUs;   // when it was due (timer clock)
};

class ClockEngine {
public:
  using Listener = void (*)(const ClockEvent &e, void *ctx);
  using Callback = void (*)(void *ctx, uint32_t arg);

  void begin(DinMidi &din, UsbMidi &usb);
  void apply(const ClockConfig &config);    // from UI; thread-safe
  void start(); void stop(); void cont();   // queued to midiTask
  void locate(uint16_t sixteenths);         // song position (when stopped)
  void tap(uint32_t micros);                // tap tempo input (touch or IMU)

  // midiTask only:
  void onExternal(const MidiMessage &msg);  // F8 FA FB FC F2 from followed input
  void runDue();                            // fire due events, re-arm timer
  bool schedule(uint64_t atUs, Callback cb, void *ctx, uint32_t arg); // sub-tick
  void addListener(Listener l, void *ctx);  // max 8; called in midiTask

  struct Snapshot { ClockMode mode; float bpm; bool running; bool locked;
                    uint32_t tick; uint16_t bar; uint8_t beat; };
  Snapshot snapshot() const;                // any task
};
```

* **Scheduler.** A fixed min-heap of 32 entries `{timeUs, cb, ctx, arg}`.
  Ticks, swung steps, multiplied output ticks, note-offs from sequencers, and
  DFAM pulse bursts (stage 20) are all just scheduled events. The timer alarm
  is always set to the earliest entry.
* **Internal clock.** Each tick's time is accumulated as a `double` (period =
  60e6 / (24 × BPM) µs) so there's no cumulative drift. A BPM change takes
  effect from the next tick.
* **Transport.**
  * **Start** sends FA; the first F8 after it is beat 1 (MIDI spec).
  * **Stop** sends FC and keeps the position.
  * **Continue** sends FB.
  * **`locate()`** sends F2 (song position) and is only allowed while stopped.
* **Swing applies only to `Step` events**, i.e. to OxiMate's own sequencers.
  **MIDI clock output stays straight**, which is the norm, so external gear
  applies its own swing. Every second 16th (or 8th) is delayed by
  `(swing − 50) / 50 × gridLength`.
* **Dividers and multipliers per output.**
  * Dividing sends every n-th tick.
  * Multiplying schedules n−1 extra ticks evenly across the current (smoothed)
    period.
  * Transport messages are sent to every enabled output.
* **Following an external clock.**
  * Incoming F8 ticks are passed straight to the outputs, with dividers
    applied, for the lowest latency. They're also used to estimate the period
    with an exponential moving average over about 24 ticks; intervals more
    than ±25% from the average are rejected as outliers.
  * The estimate drives the BPM display, multiplied ticks and swung steps.
  * The clock counts as **locked** after 24 consistent ticks. It's **lost**
    after no tick for 3 × the period or 500 ms; step events stop and the LED
    goes red.
* **Tap tempo.**
  * The tempo is the average of the last 4 intervals. Taps resume after a gap
    of more than 2 s, and taps more than ±30% from the average are ignored.
    The result is clamped to 20–300 BPM and applied from the 2nd tap.
  * If the clock is running, the last tap also realigns the beat phase.
  * Tap sources: B on the Clock page, and (optionally) tapping the case.
* **IMU tap detection** (UI thread). Poll `M5.Imu` at about 200 Hz. Detect an
  acceleration spike of more than 1.5 g above the moving baseline, with a
  150 ms dead time after each tap. Pass the timestamp to `tap()`.
* **Listener rules.** Listeners run inside `midiTask`, so they must be short
  and non-blocking: no SPI, no I2C, no `Serial`. UI reactions (LEDs,
  vibration) go through the event ring.

## Router: single clock owner (avoids double clocks)

* From stage 8 on, **the clock engine is the only thing that sends clock and
  transport** (F8, FA, FB, FC, F2) to the outputs. The router passes those
  messages to `ClockEngine::onExternal()` instead of forwarding them.
* `RouterConfig.clock` is removed: `RouterConfig::kVersion` goes to 2, and the
  first boot migrates the old setting:

  | Old `ClockSource` | New `ClockMode` |
  |-------------------|-----------------|
  | Both | FollowDin (merging two clocks was never valid) |
  | Usb  | FollowUsb |
  | Din  | FollowDin |
  | Off  | Internal, stopped, output clock disabled |

* The Router page's clock chip becomes a shortcut that cycles
  `INT / USB / DIN` and edits `ClockConfig.mode`, so there's only one setting
  to keep in sync.
* `src/clock_tracker.h` is replaced by the engine's per-input statistics. The
  header shows `INT 120.0 PLAY`, `DIN 121.3 PLAY` or `DIN --.- LOST`.

## Clock page

```
┌────────────────────────────────────────┐
│Clock>   USB 1C75:0288   INT 120.0 PLAY │  tap header: next page
│                                        │
│          1 2 0 . 0  BPM                │  big digits; drag up/down
│                                        │  to change (0.1 / 1 BPM)
│ [  INT  ]   [  USB  ]   [  DIN  ]      │  clock source
│ Bar 0012   Beat 3/4    ● ● ● ○         │  position + beat dots
│ [ -1 ][-0.1]  Swing 54%  [+0.1][ +1 ]  │
│ [Swing-][Swing+] [Grid 1/16] [IMU off] │
│   Start          Tap         Outputs   │  touch buttons A / B / C
└────────────────────────────────────────┘
```

* **A**: Start/Stop. Long-press while stopped sends Continue.
* **B**: Tap tempo.
* **C**: switch to the Outputs view and back.
* When following an external clock, the BPM controls and Tap are greyed out
  and the digits show the measured BPM (`LOST` in red when the clock drops
  out).

```
┌────────────────────────────────────────┐
│Clock>   USB 1C75:0288   INT 120.0 PLAY │
│ Output   Clock  Transport  Ratio       │
│ DIN OUT  [on ]  [on ]      [ x1 ]      │  ratio: /4 /3 /2 x1 x2 x3 x4
│ USB      [on ]  [on ]      [ x1 ]      │
│ Gate     [off]  [ - ]      [1/16]      │  stage 20 (DFAM ADV pulses)
│                                        │
│ Beats per bar  [ 4 ]                   │
│   Start          Tap          Main     │
└────────────────────────────────────────┘
```

## LED bar

While the Clock page is visible, or always if "Clock LEDs on all pages" is
on, the clock takes over the LED bar through a new renderer hook,
`Leds::setRenderer(fn)`; the default renderer is the input activity display:

| LEDs | Shows |
|------|-------|
| 0–3 | current beat in the bar: white pulse, coloured on beat 1 |
| 4 | downbeat flash |
| 5–8 | bar within a 4-bar phrase |
| 9 | sync state: green = internal, blue = external locked, red = lost |

## Metronome hook (stage 10)

* `Beat` and `Bar` events reach the UI thread through the event ring. Stage 10
  pulses the vibration motor there (`M5.Power.setVibration`, on the I2C bus).
  The added latency is about one UI loop (≤ 5 ms), which is fine for a
  metronome.

## Persistence

* `ClockConfig` is saved in NVS (namespace `oximate`, key `clock`, versioned)
  with the router's 2-second delayed save. The running state is not saved;
  OxiMate always boots stopped.

## Changes to existing modules

| File | Change |
|------|--------|
| `oximate.ino` | `setup()` starts `midiTask`; `loop()` only drains events and runs the UI |
| `src/midi_task.*` (new) | the task, event ring, timer ISR |
| `src/spi_bus.h` (new) | SPI mutex and `SpiLock` |
| `src/clock_engine.*` (new) | engine, scheduler, tap tempo, follower |
| `src/clock_page.*` (new) | Clock page |
| `src/usb_midi.*` | SPI lock, try-lock poll, TX queue; atomics for connected/VID/PID |
| `src/din_midi.*` | realtime-priority TX queues and `pumpTx()`; TX buffer 0 |
| `src/router.*` | config snapshot via `apply()`; clock messages to the engine; v2 migration |
| `src/ui.*`, `src/monitor.*` | SPI lock per draw block; banded sprite push |
| `src/leds.*` | `setRenderer()` hook |
| `src/clock_tracker.h` | removed (replaced by engine snapshot) |

## Milestones

1. **M1 Task split.** Add the SPI lock, the banded sprite push, and
   `midiTask` (owns USB, DIN and the router) feeding the event ring to the UI.
   No behaviour change: stages 1 and 2 must still work exactly as before.
2. **M2 DIN TX priority.** Realtime queue plus `pumpTx()`. Check that a
   SysEx burst no longer delays forwarded clock.
3. **M3 Internal clock.** Hardware timer, scheduler, internal clock to DIN and
   USB, Start/Stop, and a basic Clock page (BPM ±, Start).
4. **M4 Follow mode.** External follower, lock/lost detection, router v2
   migration, header from the engine snapshot. Remove `ClockTracker`.
5. **M5 Musical features.** Tap tempo (touch and IMU), swing, `Step`
   listeners, `schedule()` for consumers.
6. **M6 Outputs and polish.** Output ratios and the Outputs view, LED
   renderer, metronome events, NVS persistence.

## Test plan

* **Host unit tests** (like the parser tests) for:
  * the scheduler heap
  * tap-tempo maths
  * swing offsets
  * divider/multiplier tick patterns
  * the follower's EMA and outlier rejection
  * router v1→v2 migration
* **Jitter, internal clock to DIN.** Measure DIN OUT with a USB MIDI interface
  on a computer (e.g. a Python `mido` script logging F8 timestamps), or with a
  logic analyser on the opto output.
  * Targets at 120 BPM: **≤ 1 ms peak-to-peak** at idle, and **≤ 2 ms** while
    the Monitor scrolls heavily and a USB keyboard streams aftertouch.
* **Loopback self-test.** Patch DIN OUT → DIN IN and let the stage 9 analyser
  measure OxiMate's own clock.
* **Drift.** Run the internal clock for 10 minutes against the computer's
  clock and compare the measured BPM. Expect better than ±0.01% (crystal
  tolerance).
* **Follow latency.** With an external clock in and out, measure the added
  delay per tick. Target **< 1 ms average**.
* **Stress.** Plug and unplug USB devices while the clock runs. DIN clock must
  not stutter (USB enumeration is skipped when the SPI lock is busy, and never
  blocks the task).

## Risks

* **Two SPI drivers on one peripheral.** If M5GFX or the Arduino SPI driver
  reconfigure the bus in a way the other doesn't expect, the display or USB
  could misbehave even with the mutex. Test early in M1. Fallback: run the
  display at a lower SPI clock, or re-initialise the bus settings on each
  lock.
* **Thread safety of USB Host Shield 2.0.** All USB calls must stay inside
  `midiTask`. The UI must never call `UsbMidi` directly; it reads atomics
  instead.
* **Radio stages (17 and 18)** will contend for core 0. Measure jitter there,
  and consider moving `midiTask` to core 1.
* **Internal I2C** (touch, IMU, PMIC, vibration) is touched only by the UI
  thread. Listeners must not use it.
* **`ulTaskNotifyTake` with a 1 ms timeout** relies on
  `CONFIG_FREERTOS_HZ=1000` (the Arduino-ESP32 default). Verify it in the
  build.

## Open questions

1. Should OxiMate send clock to the **USB device** by default (e.g. an M8 on
   the USB host port), or only to DIN OUT?
2. Is it OK that **swing only affects OxiMate's own sequencers** and the MIDI
   clock output stays straight?
3. When the followed external clock **drops out**, should OxiMate stop, or
   keep running at the last tempo?
4. Fixed **4/4**, or a selectable number of beats per bar (affects the LEDs
   and the metronome)?
5. **IMU tap tempo**: off by default, because touching the screen can
   register as a tap. Agree?
