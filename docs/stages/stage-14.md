# Stage 14: SysEx librarian

> **Status: implemented 2026-11-02 (see the README stage 14 section for differences).** Written 2026-11-01. Where this plan and the README's
> [Shared infrastructure](../../README.md#shared-infrastructure) section disagree, the README wins.
> SD card access reuses stage 13's design (`src/sd_store.*`, see
> [stage-13.md, "SD card on the shared SPI bus"](stage-13.md#sd-card-on-the-shared-spi-bus)):
> whichever of stages 13 and 14 is built first creates it.

Sends `.syx` files from the SD card to a chosen output, and captures SysEx
dumps that devices send into OxiMate (DIN or USB), saving them as `.syx` files.

Use cases:

* Set the Subharmonicon's MIDI channel or switch its sequencer transport
  (the Moog files in `subH_sysex_user/`, the first test case).
* Back up and restore patches or patterns of gear that can dump over MIDI.
* Keep small "setup" files (e.g. channel or global settings) to send before a
  set.

## The first test case: Moog Subharmonicon

The 20 files in `subH_sysex_user/` are each a single, short message
(inspected byte by byte):

| File | Bytes | Meaning (from the file names) |
|------|-------|-------------------------------|
| `subharmonicon_midi_ch1.syx` … `_ch16.syx` | `F0 04 1B 23 01 00 nn 00 … 00 7F F7` (17 bytes), `nn` = channel − 1 | set MIDI channel |
| `subharmonicon_midi_ch_all.syx` | `… 23 01 00 10 …` | omni (all channels) |
| `subharmonicon_seq_transport_on.syx` / `_off.syx` | `… 23 02 00 01 …` / `… 23 02 00 00 …` | sequencer follows MIDI transport |
| `subharmonicon_fw_version.syx` | `F0 04 1B 30 F7` (5 bytes) | show the firmware version |

* `04` is Moog's manufacturer ID, `1B` the Subharmonicon.
* The firmware-version message gets **no SysEx reply**: the Subharmonicon
  shows its version on the front-panel LEDs (the SEQ 1 buttons)
  ([Moog support](https://moogmusic-help.freshdesk.com/en/support/solutions/articles/69000840528-moog-how-do-i-tell-what-firmware-version-my-synthesizer-is-on-)).
  That makes it a safe, visible send test.
* The Subharmonicon has no USB port, so these go out on DIN
  ([Moog forum](https://forum.moogmusic.com/t/subharmonicon-firmware-update-usb-midi-adapter-sysex/21920)).

## What about Elektron and the Oxi One?

* **Elektron** (manufacturer ID `00 20 3C`): the dump formats aren't publicly
  documented; people decode them from captures, and the messages are 7-bit
  packed with checksums
  ([Elektronauts: decoding the Digitone SysEx](https://www.elektronauts.com/t/decoding-the-digitone-sysex/62731?page=5),
  [Digitakt MIDI MCP server README](https://glama.ai/mcp/servers/@feamster/digitakt-midi-mcp/blob/0cf719bbeddf089692d5d5e224b3763df4c0e837/README.md)).
  The librarian doesn't need to understand them: it captures whatever the
  device sends and plays it back byte for byte. Dumps can be large (patterns
  and kits hold a lot of data), so capture must not be limited to 1 KB.
* Elektron's own C6 tool sends with a configurable **delay between packets**
  ("typical amounts would be 1 or 2" ms; increase it when transfers fail)
  ([C6 manual](https://www.manualsdir.com/manuals/657173/elektron-c6.html?page=4)).
  The librarian gets the same setting.
* **Oxi One** (manufacturer ID `00 21 5B`, see stage 3): its SysEx is the
  remote protocol, not backups; project backups go through its USB storage
  (not verified). Nothing special is needed here.

## Sending

* **Parsing a `.syx` file:** split it into messages from `F0` to `F7`.
  Bytes outside a message are skipped and counted (some files have stray
  bytes); a message without `F7` at the end of the file is dropped with a
  warning. Files are read from the SD card in 4 KB chunks into a PSRAM send
  buffer (up to 1 MB per file; larger files are refused).
* **Gaps:** a setting, **gap between messages**: 0, 2, 10, 20, 50, 100 or
  250 ms (default 20 ms). Many devices need time to store each message
  (flash writes); a single message never needs a gap.
* **Why a new send path:** today a message must fit in one go:
  `DinMidi::send()` queues whole messages only (2 KB queue), `MidiTask::send()`
  takes at most 16 bytes, and the USB send queue holds 2 KB. A large dump
  message would be dropped.
* **SysEx send job in the MIDI task** (`src/sysex_sender.*`, the same pattern
  as the stage 16 panic job): the UI hands over a pointer to the PSRAM buffer,
  the message offsets and the output. Each MIDI task loop the job feeds as
  many bytes as the output queue has room for:
  * **DIN:** a new `DinMidi::sendRaw()` appends part of a message. While a
    SysEx is in progress the job **owns the DIN stream**: other non-realtime
    DIN output (router, generators) is held back until its `F7` is queued,
    because anything inserted would end the SysEx. Realtime bytes (clock)
    still go through: MIDI allows them inside SysEx, and the DIN realtime
    queue already sends them first.
  * **USB:** USB-MIDI carries SysEx as 3-byte packets (CIN 4, ending with
    CIN 5/6/7). A new `UsbMidi::sendSysExPart()` builds these packets
    directly and queues them, so a message of any length streams through the
    existing 2 KB send queue.
  * The gap is timed with the clock engine's scheduler (`schedule()`), not
    with a delay, so nothing blocks.
* **Progress:** the job publishes bytes sent, message number and state in a
  snapshot for the page (`CriticalSection`, as everywhere).
* **Speed:** DIN carries about 3 KB/s, so a 100 KB dump takes about 35 s
  plus gaps; the page shows an estimate before sending.

## Capturing

* **Problem:** the DIN and USB parsers assemble SysEx into a 1024-byte
  buffer and drop anything longer, and the event ring keeps only the first
  16 bytes of each message.
* **Streaming capture path:** add a **SysEx tap** to the input hooks
  (`InputHooks::addSysExTap()`): the DIN parser and the USB packet parser
  call it with each chunk as it arrives (`source, bytes, length, start,
  end`), independent of the 1024-byte limit. Assembly for normal routing
  stays as it is.
* **Capture buffer:** while capturing, the tap appends to a PSRAM buffer
  (1 MB, ring-safe: a full buffer stops the capture with a message). The MIDI
  task only copies bytes; the UI side does everything else.
* **When a capture ends:** after **2 s** without SysEx (configurable), or
  when **Stop** is tapped. The page then shows the number of messages and
  bytes, the manufacturer (from the ID: `04` Moog, `00 20 3C` Elektron,
  `00 21 5B` Oxi, `41` Roland, `43` Yamaha, …) and offers **Save**.
* **File names:** `/oximate/dumps/<maker>-<date>-<time>.syx` (e.g.
  `elektron-20261101-213045.syx`), using the Core2's RTC (`M5.Rtc`). If the
  RTC was never set (year < 2024), a counter is used instead
  (`elektron-0007.syx`).
* **While capturing, the router doesn't forward SysEx** from the capturing
  input (a "capture only" switch, on by default). That way a dump doesn't also
  flood DIN OUT, and a device with MIDI thru can't loop it back.
* **Request first, then capture:** a `.syx` file in `/oximate/requests/` can
  be sent with **Send + capture**, which starts the capture right after the
  request (for devices that dump on request).
* **Own echoes:** if a device echoes the SysEx we just sent (MIDI thru, as
  the Oxi did in testing), the capture would record it. Capture pauses while
  a send is running, and stops recording bytes that match what was just sent.

## Safety

* **Every send names its output: DIN, USB, or both.** There is no default
  "everywhere": the first send asks, after that the page remembers the
  choice per folder. The confirmation shows file, output, message count and
  size.
* **Broadcast warning:** universal SysEx (`7E`/`7F`) with device ID `7F`
  reaches every device on that output, including the SAM2695 (it shares the
  wire with DIN OUT). The confirmation shows "broadcast to all devices on
  DIN" for those.
* **SAM2695:** it ignores manufacturer SysEx it doesn't know. A GM/GS reset
  in a file resets it, and stage 6's `SamSynth` already re-applies its
  settings afterwards.
* **Panic** (power button) cancels a running send: it stops after the
  current message's `F7`, so no device is left inside an unterminated SysEx.

## Page

The page is called **SysEx** and has three views: Library, Send and
Capture. Swipe or tap the header to change pages, as everywhere.

```
┌────────────────────────────────────────┐
│SysEx>   USB ok  INT 120.0 STOP  ▮▮ 87% │
│ /oximate/syx/subharmonicon/            │  current folder
│>midi_ch1.syx            17 B   1 msg   │  tap a row to select
│ midi_ch2.syx            17 B   1 msg   │
│ midi_ch_all.syx         17 B   1 msg   │
│ seq_transport_on.syx    17 B   1 msg   │
│ fw_version.syx           5 B   1 msg   │
│ ..                                     │  up one folder
│ [Out: DIN] [Gap 20 ms] [Capture]       │  output, gap, capture view
│    Up          Send          Down      │  touch buttons A / B / C
└────────────────────────────────────────┘
```

```
┌────────────────────────────────────────┐
│SysEx>   USB ok  INT 120.0 STOP  ▮▮ 87% │
│ Sending midi_ch1.syx to DIN            │
│ Message 1 / 1                          │
│ [██████████████████████████████] 100%  │
│ 17 / 17 bytes   0.1 s                  │
│                                        │
│ Done.                                  │
│                                        │
│    Back                      Again     │
└────────────────────────────────────────┘
```

```
┌────────────────────────────────────────┐
│SysEx>   USB ok  INT 120.0 STOP  ▮▮ 87% │
│ Capture from [DIN] [USB]  capture only │
│ Waiting for SysEx...                   │
│ Elektron  12 messages  48 213 bytes    │
│ last byte 0.4 s ago                    │
│ File: elektron-20261101-213045.syx     │
│                                        │
│                                        │
│    Stop         Save         Discard   │
└────────────────────────────────────────┘
```

* Library: A/C move the selection, B sends (after the confirmation).
  Tapping a folder opens it. Hold B: **Send + capture**.
* Send: A cancels (after the current message), C sends the same file again.
* Capture: A stops, B saves, C discards.
* Without an SD card: Library shows "No SD card"; capture still works and can
  be sent back (from PSRAM) but not saved.

## LED bar

* While sending: a progress bar on all 10 LEDs (cyan).
* While capturing: a slow red pulse; LEDs 0–4 (USB) or 5–9 (DIN) flicker with
  incoming bytes.
* The panic overlay still wins.

## Settings (NVS)

Key `sysex`, versioned: last output per folder (up to 8 folders), gap, capture
inputs, idle timeout, capture-only switch.

## Files

| File | Purpose |
|------|---------|
| `src/syx_file.*` | `.syx` parsing into messages, manufacturer names (host-tested) |
| `src/sysex_sender.*` | send job in the MIDI task: streaming, gaps, progress, cancel |
| `src/sysex_capture.*` | SysEx tap target, PSRAM capture buffer, idle detection, own-echo filter |
| `src/sysex_page.*` | the SysEx page |
| `src/sd_store.*` | shared with stage 13 (created by whichever stage comes first) |
| `src/din_midi.*` | `sendRaw()`, the tap call in the parser, holding back other output during a SysEx |
| `src/usb_midi.*` | `sendSysExPart()`, the tap call in the packet parser |
| `src/input_observers.h` | `addSysExTap()` |
| `src/router.*` | "capture only": don't forward SysEx from the capturing input |

## Milestones

1. `syx_file` parser and manufacturer names, host-tested; the Subharmonicon
   files as test data.
2. Send job with DIN `sendRaw()` and USB `sendSysExPart()`, gaps, cancel;
   send a file built into the firmware (no SD yet): the Subharmonicon
   firmware-version message, checked on the Subharmonicon's LEDs.
3. SysEx tap and capture to PSRAM, idle detection, capture-only routing.
4. SD store (shared with stage 13): browse, load, save; the page's three views.
5. Send + capture, own-echo filter, broadcast warning, settings, README and
   TESTING.md section.

## Host tests

* Parser: files with one and many messages, stray bytes between messages, a
  missing `F7` at the end, all 20 Subharmonicon files.
* Send job (fake time and fake DIN FIFO): bytes come out in order, gaps are
  honoured, other DIN messages wait until `F7`, clock bytes still pass, cancel
  stops after `F7`; USB packets use the right CINs for lengths that end with
  1, 2 and 3 bytes.
* Capture: a 50 KB dump split across DIN bytes (with clock bytes inside) and
  across USB packets is captured byte-exact; idle timeout; buffer full; own
  echo is ignored.
* File naming with and without a set RTC.

## Risks

* **Unknown device timing:** some devices drop data without a gap; the gap
  setting covers it, but the right value is found by trying.
* **Holding back DIN output during long SysEx:** a 35 s dump blocks other
  DIN output (except clock) for that time. The confirmation says so; sending
  is not meant for the middle of a song.
* **SD on the shared bus:** as in stage 13. Loading happens before a send,
  so SD and MIDI traffic don't overlap much.
* **RTC not set:** the counter fallback avoids broken names; setting the RTC
  (from the computer over serial, or by hand) is a separate small feature.

## Open questions

1. Which devices do you want to back up or restore by SysEx (Elektron
   models, others)? That decides how big dumps get and whether request files
   are needed.
2. Is 20 ms a good default gap, or should it start at 0 for speed?
3. Should captured dumps be sendable straight away without an SD card (from
   PSRAM only), or is SD always present?
4. Do you want a small "set clock" feature (date and time for file names), or
   are counter names fine?
