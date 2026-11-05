#pragma once

#include <atomic>
#include "packet_ring.h"
#include "radio.h"
#include "rtp_midi.h"

/*! Network MIDI over Wi-Fi (stage 18): an AppleMIDI / RTP-MIDI session with a Mac or iPad.
 *
 *  - Own access point by default ("OxiMate-xxxx", WPA2, password shown on the Radio page; OxiMate
 *    is 192.168.4.1). There a phone or the Mac can open http://192.168.4.1 to enter a network to
 *    join instead (saved, then OxiMate reboots into it).
 *  - Joining a network (studio): if it can't be joined within 20 s, OxiMate opens its own access
 *    point again, so the setup page stays reachable.
 *  - mDNS announces the session (`_apple-midi._udp`, "OxiMate"): Audio MIDI Setup > Network on a
 *    Mac, or a network session app on the iPad; OxiMate answers the invitation.
 *  - Wi-Fi power save is off while network MIDI runs (decided 2026-11-05): lower latency, more drain.
 *
 *  Threads: a network task (core 0) owns the sockets, the session and the setup page; complete
 *  messages cross to and from the MIDI task through rings. */
class NetMidi : public RadioTransport
{
public:
  static constexpr const char *kName = "OxiMate";
  static constexpr uint32_t kJoinTimeoutMillis = 20000;

  bool begin(const RadioConfig &config, MidiHandler handler) override;
  void poll() override;
  void status(char lines[kStatusLines][kStatusLength]) const override;

  bool connected() const override { return _connected.load(); }
  size_t txSpace() const override;
  void send(const MidiMessage &msg) override;

  // Network task only
  void networkTask();
  void onReceived(const MidiMessage &msg);
  void sendUdp(bool dataPort, AppleMidiSession::Endpoint to, const uint8_t *data, size_t length);
  void scheduleReboot(); ///< the setup page saved a network

private:
  void startAccessPoint();
  void startServices();
  void serveSetup();

  MidiHandler _handler = nullptr;
  RadioConfig _config;
  PacketRing _rx, _tx; ///< whole messages: network task -> MIDI task, MIDI task -> network task
  AppleMidiSession _session;
  RtpMidiEncoder _encoder;
  uint8_t _message[kMaxSysExLength]; ///< poll(): one received message (not on the MIDI task's stack)
  static constexpr size_t kPacketBytes = 1500;
  uint8_t _packet[kPacketBytes]; ///< network task: one UDP datagram

  std::atomic<bool> _connected{false};
  std::atomic<bool> _accessPoint{false}; ///< own access point (by choice, or after a failed join)
  std::atomic<bool> _joinFailed{false};
  std::atomic<bool> _online{false};      ///< have an address, services running
  std::atomic<uint32_t> _ip{0};
  char _apName[20] = "";
  char _apPassword[16] = "";
  char _peerName[24] = "";               ///< copy for the UI
  std::atomic<uint32_t> _latency{0};
  std::atomic<uint32_t> _rxPackets{0}, _txPackets{0}, _lost{0};
  uint32_t _rebootAt = 0;                ///< setup page saved a network
};
