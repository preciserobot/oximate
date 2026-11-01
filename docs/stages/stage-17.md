# Stage 17: BLE MIDI bridge

> **Status: plan, not implemented.** Written 2026-11-01. Where this plan and the README's
> [Shared infrastructure](../../README.md#shared-infrastructure) section disagree, the README wins.
> Shares the "Radio" mode switch, the N-input/N-output generalisation and the route matrix with
> stage 18 (network MIDI). Whichever of 17 and 18 is built first does that groundwork (M1–M2).

Wireless MIDI between OxiMate and an iPad, iPhone or Mac (OxiMate as a BLE MIDI
**peripheral**), and optionally from a BLE MIDI controller to OxiMate (OxiMate as a
**central**). BLE MIDI becomes a third input and a third output, routed and merged like
USB and DIN.

## Goal and use cases

* Play an iPad synth (or record into a Mac/iPad DAW) from the Oxi One, a USB keyboard or
  the Elektrons, through OxiMate, without cables.
* Control OxiMate's outputs from an iPad app (e.g. a touch sequencer) over BLE.
* Optional: pair a BLE MIDI controller (e.g. a small BLE keyboard or pad) as an extra input.
* Not a goal: sample-accurate clock over BLE (see "Clock over BLE").

## BLE MIDI in short

| Topic | Details |
|-------|---------|
| Spec | "MIDI over Bluetooth Low Energy" (BLE-MIDI 1.0): one GATT service `03B80E5A-EDE8-4B33-A751-6CE34EC4C700`, one characteristic `7772E5DB-3868-4112-A1A9-F2669D106BF3` (read, write without response, notify) |
| Packets | Header byte (bit 7 set, 6 high bits of a 13-bit millisecond timestamp), then per message a timestamp-low byte and the MIDI bytes; running status allowed; SysEx may span packets |
| Throughput | One notification per connection event per direction in practice; 20 data bytes at the default MTU (23), more after MTU exchange (Apple accepts 185) |
| Latency | One connection interval plus processing. Apple's accessory guidelines ask BLE MIDI devices to request **11.25 ms**; iOS/macOS often settle on 11.25–15 ms. Expected: **~10–20 ms latency, up to one interval of jitter** unless the receiver honours the timestamps (macOS/iOS CoreMIDI does) |

## Libraries: NimBLE, own thin transport

* **Stack: NimBLE** (NimBLE-Arduino 2.5.0 is installed). Compared with Bluedroid it needs
  roughly half the internal RAM (tens of KB instead of ~100 KB) and much less flash; NimBLE is
  the usual choice on the ESP32 when Wi-Fi is not needed at the same time.
* **Not the installed BLE-MIDI 2.2 library as is.** Its NimBLE transport
  (`hardware/BLEMIDI_ESP32_NimBLE.h`) uses the NimBLE 1.x callback signatures
  (`onConnect(BLEServer*)`, `onWrite(BLECharacteristic*)`), while NimBLE-Arduino 2.x expects
  `onConnect(NimBLEServer*, NimBLEConnInfo&)` and `onWrite(NimBLECharacteristic*,
  NimBLEConnInfo&)`. With 2.5.0 the callbacks would silently not override. It also routes
  everything through the MIDI Library's parser and a 64-byte queue.
* **Plan:** a small own transport, `src/ble_midi.*`, on NimBLE 2.x:
  * the GATT service and characteristic, advertising, connection and MTU handling;
  * a **packet codec** (`src/ble_midi_codec.*`, plain C++, host-tested): BLE-MIDI packets
    ⇄ complete `MidiMessage`s, including timestamps, running status, realtime bytes inside
    SysEx and SysEx across packets;
  * output batching: messages queued by the MIDI task are packed into one notification per
    ~2 ms (or per connection event), up to the negotiated MTU.

## Architecture

```
 NimBLE host task (core 0, prio ~21)          MIDI task (core 0 or 1, prio 10)
 ┌───────────────────────────┐   rx ring    ┌───────────────────────────────┐
 │ onWrite: raw packet bytes │ ───────────► │ BleMidi::poll(): decode,      │
 │ onConnect / onDisconnect  │              │   onMidi(MidiSource::Ble)     │
 │                           │ ◄─────────── │ MidiOutputs::send(Ble): queue │
 │ notify (from flush)       │   tx ring    │ BleMidi::flush() every ~2 ms  │
 └───────────────────────────┘              └───────────────────────────────┘
```

* NimBLE callbacks only copy raw packet bytes into an SPSC ring (like the USB TX queue);
  all decoding, routing and tracking happens in the MIDI task, so the threading rules of the
  rest of OxiMate stay intact.
* `NimBLECharacteristic::notify()` is called from the MIDI task's flush (NimBLE's API is
  thread-safe for that); one notification per flush.
* Timestamps: incoming messages get `timeMicros` reconstructed from the packet timestamp
  (relative to arrival), so the clock analyser and follow mode can use them; outgoing packets
  carry the send time.

## Core and priority: when the radio is on

The README notes that the radio stacks run on core 0 at priorities 18–23 and would preempt
the MIDI task (core 0, priority 10).

| Load on core 0 with BLE connected | Estimate |
|---|---|
| BT controller + NimBLE host per connection event | ~0.2–0.5 ms every 11.25–15 ms |
| Worst case (MTU exchange, pairing, scan) | several ms |
| Effect on the MIDI task | added jitter of ~0.5 ms typical, a few ms during pairing |

* **Plan:** a constant `MidiTask::kCore` becomes a boot-time choice: with the radio off the
  MIDI task stays on core 0 as now; with BLE or Wi-Fi enabled it runs on **core 1** at
  priority 10 (it still outranks `loopTask`, priority 1). Its work is ~50–200 µs per 1 ms
  loop, so it takes ~5–20 % of core 1 and the UI gets slightly slower redraws.
* Measured, not assumed: milestone 3 compares USB gap max and DIN clock jitter (stage 9
  Analyse page, plus the serial status line) with BLE off, BLE on/core 0 and BLE on/core 1,
  and fixes the default from that.

## New input and output: generalise from 2 to N

Today several arrays have exactly two entries indexed by `MidiSource` or `MidiOutput`:
`RouterConfig::channelMask/forceChannel`, `Stats` counters, `Leds::_flash`,
`NoteTracker::_state`, `MidiOutputs::Counters`, `Panic` cursors and summary, the clock
analyser's per-input stats, and later stages' per-input state.

* M1 introduces `kInputs` / `kOutputs` constants next to the enums, sizes every array with
  them, and adds `MidiSource::Ble` and `MidiOutput::Ble` (stage 18 adds `Net`).
* **Router: a route matrix.** `RouterConfig` v3 replaces `usbToDin / dinToDin / dinToUsb`
  with `uint8_t routes[kInputs]` (bit per output), migrated from v2 (same behaviour). The
  "never back to where it came from" rule stays for USB and BLE (a BLE peer would hear its
  own notes); DIN keeps its software thru.
* Router page: the three route chips become a small matrix (rows = inputs, columns =
  outputs), only showing BLE/Net when the radio mode enables them.
* Clock: `ClockOutput` gains `Ble` (clock/transport off by default) and `ClockMode` gains
  `FollowBle` (allowed, with the jitter warning below).
* LEDs: BLE activity flashes blue on both sides (there are only two strips).
* Panic and automatic release: BLE disconnect releases notes from that input and clears
  the BLE output's tracker (same as USB unplug).

## Clock over BLE

* Sending clock to an iPad/Mac: off by default. With 24 PPQN at 120 BPM a tick is every
  20.8 ms, longer than one connection interval, so ticks arrive one per packet with their
  timestamps; CoreMIDI uses the timestamps, so apps that sync to MIDI clock are usable but
  less tight than USB or DIN. Transport (Start/Stop) is fine.
* Following a BLE clock: allowed, but the clock engine's 24-tick average will show visible
  jitter; the Clock page shows a "BLE: loose" hint. Recommended setup stays: OxiMate or a
  DIN/USB device as master, BLE as follower.

## Pairing and the Radio page

A new "Radio" page (shared with stage 18):

```
┌────────────────────────────────────────┐
│Radio>   USB ok  INT 120.0 PLAY   87%   │
│ Mode  [Off] [BLE] [Wi-Fi]              │  change → saved, reboot
│ BLE name   OxiMate                     │
│ Peripheral: advertising                │  or "connected: iPad (MTU 185)"
│ Central:  [Scan]  BLE-Keys  -61 dBm    │  tap a found device to connect
│ Interval 11.25 ms  rx 1203  tx 980     │
│ Clock to BLE [off]   Follow BLE [no]   │
│                                        │
│   Forget        Scan        Reboot     │  touch buttons A / B / C
└────────────────────────────────────────┘
```

* **Peripheral (default):** OxiMate advertises as "OxiMate"; on iOS connect from the app's
  Bluetooth MIDI settings (e.g. GarageBand / AUM), on macOS from Audio MIDI Setup →
  MIDI Studio → Bluetooth. No PIN; "Just Works" bonding so the peer reconnects automatically.
* **Central (optional, M5):** scan for the BLE-MIDI service UUID, connect to one controller,
  remember it, reconnect on boot.
* **Forget** removes bonds. The mode change is stored in NVS and applied by a reboot (see
  stage 18 for why).

## Settings (NVS)

Key `radio` (shared with stage 18), versioned:

```cpp
struct RadioConfig {
  static constexpr uint8_t kVersion = 1;
  uint8_t version = kVersion;
  RadioMode mode = RadioMode::Off;   // Off, Ble, Wifi
  char bleName[20] = "OxiMate";
  bool bleCentral = false;
  uint8_t centralAddress[6] = {};    // remembered controller
  bool clockToBle = false;
  // stage 18: Wi-Fi fields
};
```

## Files

| File | Change |
|------|--------|
| `src/midi_message.h`, `src/midi_outputs.h` | `kInputs`/`kOutputs`, `Ble` enum values |
| `src/router.*`, `src/router_page.*` | route matrix, v3 migration |
| `src/ble_midi_codec.*` (new) | BLE-MIDI packet encode/decode (host-tested) |
| `src/ble_midi.*` (new) | NimBLE service, rings, flush, central scan |
| `src/radio.*` (new, shared with 18) | `RadioConfig`, boot-time mode, core choice for the MIDI task |
| `src/radio_page.*` (new, shared with 18) | Radio page |
| `src/midi_task.*` | core chosen at boot; `BleMidi::poll()/flush()` in the loop |
| `src/clock_engine.*`, `src/leds.*`, `src/panic.*`, `src/diagnostics.h` | array sizes, BLE output/clock options |

## Milestones

1. **N inputs/outputs** (shared with 18): `kInputs`/`kOutputs`, arrays, route matrix with
   v2→v3 migration, Router page matrix. No radio yet; all tests and hardware behaviour
   unchanged.
2. **Radio mode and boot path** (shared with 18): `RadioConfig`, Radio page skeleton,
   reboot on change, MIDI task core chosen at boot.
3. **BLE peripheral**: codec + NimBLE service, notes both ways with a Mac/iPad; measure
   latency and jitter on core 0 vs core 1 (Analyse page, USB gap, DIN clock jitter) and pick
   the default core.
4. **Routing, panic, tracker, LEDs, Diag counters** for BLE; MTU negotiation; batching.
5. **Central mode** (optional): scan, connect, remember, reconnect.
6. Clock to/from BLE options, README and TESTING.md section, commit.

## Test plan

* **Host:** codec golden tests: single messages, several per packet with timestamps,
  running status, realtime inside a message and inside SysEx, SysEx across 3 packets,
  timestamp wrap at 8192 ms, malformed packets dropped; route-matrix migration v2→v3;
  route rules (no echo to the input it came from).
* **Hardware:**
  * iPad (GarageBand/AUM) and Mac (Audio MIDI Setup) connect, reconnect after reboot.
  * Notes and CCs both ways; a 50-note chord burst arrives complete.
  * Latency with a loopback (DIN out → DIN in, BLE in the middle) on the Analyse page.
  * USB gap max and DIN clock jitter with BLE connected, core 0 vs core 1.
  * Disconnect while holding notes: no stuck notes on DIN or at the iPad.

## Risks

* **Timing:** BLE adds 10–20 ms and up to one interval of jitter; good for playing
  soft synths, not for tight clock. Documented on the page.
* **Core 0 contention:** mitigated by moving the MIDI task to core 1 when the radio is on.
* **Memory:** NimBLE needs tens of KB of internal RAM; current free heap (Diag page) must be
  checked before M3. Large buffers stay in PSRAM.
* **iOS/macOS quirks:** connection parameter requests can be ignored; the page shows the
  actual interval.
* **Library maintenance:** own transport means owning the BLE-MIDI codec; mitigated by host
  tests.

## Open questions

1. What will connect over BLE: an iPad, a Mac, or both? Which apps?
2. Is OxiMate as a central (BLE controller → OxiMate) needed, or only peripheral?
3. Should BLE input be routed to DIN by default, or off until enabled on the Router page?
4. Is a reboot when switching the radio mode acceptable?
5. Any need for clock to the iPad/Mac over BLE, given the jitter?
