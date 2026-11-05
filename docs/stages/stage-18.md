# Stage 18: Network MIDI

> **Status: implemented 2026-11-05 (needs hardware test; see the README for the differences).** Written 2026-11-01. Where this plan and the README's
> [Shared infrastructure](../../README.md#shared-infrastructure) section disagree, the README wins.
> Shares the "Radio" mode switch, the N-input/N-output generalisation and the route matrix with
> stage 17 (BLE MIDI). Whichever of 17 and 18 is built first does that groundwork (M1–M2).
>
> **Decided 2026-11-05:**
>
> * Switching radio mode reboots: accepted. Network input is **routed by default**. **No clock**
>   over the network.
> * Hosts: **Mac or iPad** (both have built-in RTP-MIDI session support).
> * OxiMate makes **its own network** by default; joining an existing network is an option.
> * Wi-Fi power save is **always off while network MIDI is on**.

MIDI between OxiMate and a Mac (or any RTP-MIDI host) over Wi-Fi, using the
**RTP-MIDI / AppleMIDI** protocol that macOS supports natively (Audio MIDI Setup →
Network). The network session becomes a fourth input and output.

## Goal and use cases

* Send the merged stream (Oxi One, keyboards, Elektrons) to a Mac DAW and receive MIDI
  from it, without a USB cable, and with more bandwidth and lower jitter than BLE.
* At a gig there is usually no usable Wi-Fi: OxiMate can be its **own access point**, and
  the Mac joins it directly.
* Not a goal: internet access, web UI for everything (only Wi-Fi setup).

## Protocol and library

| Topic | Details |
|-------|---------|
| Protocol | RTP-MIDI (RFC 6295) with Apple's session protocol (AppleMIDI): control port 5004, data port 5005 (UDP); invitation, clock sync and receiver feedback; a recovery journal for lost packets |
| Discovery | Bonjour/mDNS service `_apple-midi._udp`, so OxiMate shows up in Audio MIDI Setup's Network directory |
| Library | [Arduino AppleMIDI Library](https://github.com/lathoub/Arduino-AppleMIDI-Library) by lathoub (v3.x): ESP32 over `WiFiUDP`, built on the FortySevenEffects MIDI Library (installed, 5.0.2); default session "AppleMIDI-Arduino" on port 5004 ([wiki](https://www.github.com/lathoub/Arduino-AppleMIDI-Library/wiki)) |
| Known issues | ESP32 issues in the tracker: a stack-smashing error with the default Bonjour name, and a report of not answering rtpMIDI invitations; to be checked in M3 (use a custom session/Bonjour name, give the network task enough stack) |

* The library is not installed yet; M3 adds it (`arduino-cli lib install AppleMIDI`), pinned
  to a version, like USB Host Shield 2.0.
* Alternative if the library misbehaves: own minimal AppleMIDI session + RTP-MIDI packet
  codec (the session protocol is small; the recovery journal can be "empty journal" at first,
  which macOS accepts but then can't recover lost packets).

## Wi-Fi modes

| Mode | When | How |
|------|------|-----|
| **Access point (default)** | gigs, rehearsal without Wi-Fi | OxiMate creates "OxiMate-xxxx" (WPA2, password shown on the Radio page); the Mac joins it, OxiMate is 192.168.4.1, mDNS works on that network |
| **Station** | studio | joins a saved network; mDNS announces the session |

* **Power save off** (`WiFi.setSleep(false)` / `WIFI_PS_NONE`): with modem sleep the ESP32
  only wakes at DTIM beacons, which adds tens to ~100 ms of latency. Cost: the radio stays
  on, roughly 100+ mA extra (see Risks); the header's battery indicator will show it.
* Latency on a quiet network: typically a few ms with a few ms of jitter; on a busy venue
  Wi-Fi much worse — another reason for AP mode.

## Entering Wi-Fi credentials

Two ways, both on the Radio page:

1. **Setup portal (recommended):** "Setup" starts a temporary access point and a tiny web
   page (captive portal, DNS redirect); a phone connects, picks the network from a scan list
   and types the password with its own keyboard. Credentials go to NVS; OxiMate reboots into
   station mode.
2. **On-screen keyboard (fallback):** a 4-row QWERTY keyboard (10 keys of 32 px per row,
   shift/symbols), usable but slow on a 2" screen.

## Architecture

```
 Wi-Fi / lwIP tasks (core 0, prio 18–23)        MIDI task (core 1 when radio on, prio 10)
 ┌──────────────────────────────┐   rx ring   ┌──────────────────────────────────┐
 │ network task (own, prio ~5): │ ──────────► │ NetMidi::poll(): onMidi(Net)      │
 │  AppleMIDI.read() / sessions │             │ MidiOutputs::send(Net): tx ring   │
 │  mDNS, sync, journal         │ ◄────────── │                                  │
 └──────────────────────────────┘   tx ring   └──────────────────────────────────┘
```

* The AppleMIDI library is polled from its own **network task** (UDP sockets, session
  housekeeping), not from the MIDI task, so a slow socket never delays DIN/USB. Complete
  messages cross to the MIDI task through SPSC rings, as for BLE.
* Outgoing: the network task drains the tx ring every 1–2 ms and sends one RTP packet with
  all pending messages (the library batches per call).
* Timestamps: incoming messages are stamped on arrival in the network task (RTP timestamps
  could refine this later).

## Coexistence with BLE: one radio mode at a time

* The ESP32 can run Wi-Fi and BLE together (time-sliced coexistence), but both stacks use
  internal RAM, and coexistence adds latency to both. The README already expects separate
  modes.
* **Radio mode Off / BLE / Wi-Fi**, stored in NVS (`RadioConfig`, shared with stage 17).
  Changing it **reboots** OxiMate: initialising one stack at boot avoids fragmentation from
  deinit/reinit and keeps memory use predictable. The MIDI task runs on core 1 whenever a
  radio is on (see stage 17 for the numbers).

## New input and output

Same groundwork as stage 17: `MidiSource::Net`, `MidiOutput::Net`, route matrix row and
column, Router page matrix, clock output `Net` (clock/transport **on** by default here,
since a DAW often follows clock; tighter than BLE), `ClockMode::FollowNet`, Diag counters,
LEDs (network activity flashes green on both sides), panic and release on session end.

## Radio page (Wi-Fi part)

```
┌────────────────────────────────────────┐
│Radio>   USB ok  INT 120.0 PLAY   72%   │
│ Mode  [Off] [BLE] [Wi-Fi]              │
│ Wi-Fi [AP] [Station]                   │
│ AP  OxiMate-3F2A  pw: tekno-4821       │  shown in AP mode
│ IP 192.168.4.1   mDNS oximate.local    │
│ Session: MacBook (connected) 3 ms      │  peer name, measured latency
│ rx 4021  tx 3980  lost 0               │
│   Setup         Keys        Reboot     │  A portal, B on-screen keyboard
└────────────────────────────────────────┘
```

## Settings (NVS)

`RadioConfig` (key `radio`, shared with 17) gains: `wifiMode` (AP/Station), `apName`,
`apPassword` (generated on first boot), `ssid[33]`, `password[65]`, `sessionName[24]`
("OxiMate"), `clockToNet`.

## Files

| File | Change |
|------|--------|
| `src/net_midi.*` (new) | Wi-Fi start (AP/station), AppleMIDI session, mDNS, network task, rings |
| `src/wifi_portal.*` (new) | setup access point, scan list, credentials form |
| `src/keyboard_view.*` (new) | on-screen keyboard (reusable) |
| `src/radio.*`, `src/radio_page.*` | shared with 17: Wi-Fi fields and page section |
| shared groundwork | as in stage 17 (enums, arrays, route matrix, clock output) |

## Milestones

1. N inputs/outputs + route matrix (shared with 17, if not done yet).
2. Radio mode + boot path + core choice (shared with 17).
3. AP mode + AppleMIDI session with a Mac: notes both ways; check the known ESP32 issues;
   measure latency/jitter (Analyse page with a DIN loopback through the Mac) and power draw.
4. Routing, clock to/from Net, panic and session-end release, Diag counters, LEDs.
5. Station mode with credentials via setup portal; on-screen keyboard fallback.
6. README and TESTING.md section, commit.

## Test plan

* **Host:** route matrix with a fourth input/output; config migration; credential struct
  bounds; (if an own codec is written) RTP-MIDI packet encode/decode golden tests.
* **Hardware:**
  * Mac joins the OxiMate access point; OxiMate appears in Audio MIDI Setup → Network;
    connect; notes and CCs both ways; 100-message burst complete.
  * DAW follows OxiMate's clock over the network; jitter on the DAW side acceptable.
  * Latency and jitter vs USB/DIN on the Analyse page (loopback via the Mac).
  * USB gap max and DIN clock jitter with Wi-Fi on (MIDI task on core 1).
  * Battery drain over 30 minutes with Wi-Fi on vs off.
  * Session dropped (Mac sleeps) while notes are held: no stuck notes.

## Risks

* **Power:** Wi-Fi with power save off costs on the order of 100 mA continuously; with the
  Core2 + GO Bottom2 batteries that noticeably shortens battery life. The page shows it; USB
  power recommended for long sets.
* **Venue Wi-Fi:** unpredictable latency; AP mode avoids it.
* **Library on ESP32:** known issues (see above); fallback is an own minimal implementation.
* **Memory:** Wi-Fi + lwIP need tens of KB of internal RAM; checked on the Diag page before
  M3; only one radio at a time.
* **Security:** AP password generated per device and shown on screen; no web UI beyond setup.

## Open questions

1. Will this be used with a Mac (Audio MIDI Setup), Windows (rtpMIDI by Tobias Erichsen),
   or an iPad (network sessions)?
2. Access point mode by default (no venue Wi-Fi needed), or do you have a studio network
   OxiMate should join?
3. Should the DAW receive the merged stream by default, or only selected routes?
4. Is the reboot when switching Off / BLE / Wi-Fi acceptable?
5. Battery vs latency: always power save off (low latency, more drain), or a switch?
