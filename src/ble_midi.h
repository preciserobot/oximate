#pragma once

#include <atomic>
#include "ble_midi_codec.h"
#include "radio.h"

/*! BLE MIDI over NimBLE-Arduino 2.x (stage 17). Own thin transport: the installed BLE-MIDI
 *  library's NimBLE transport doesn't match NimBLE 2.x callbacks.
 *
 *  - Peripheral, always: advertises "OxiMate" with the BLE MIDI service; iPad / Mac connect to it
 *    (iOS: an app's Bluetooth MIDI dialog, macOS: Audio MIDI Setup > Bluetooth).
 *  - Central, optional: connects to one remembered BLE MIDI device (a controller, the Oxi One),
 *    picked from a scan on the Radio page, and reconnects to it whenever it is around.
 *
 *  Threads: NimBLE callbacks (core 0) only copy packets into rings; poll() on the MIDI task decodes
 *  them and transmits what send() packed; connecting and scanning run in a small task of its own
 *  (connecting blocks for seconds). Messages from all connected devices arrive as input BLE. */
class BleMidi : public RadioTransport
{
public:
  static constexpr const char *kDeviceName = "OxiMate";

  bool begin(const RadioConfig &config, MidiHandler handler) override;
  void poll() override;
  void status(char lines[kStatusLines][kStatusLength]) const override;

  bool connected() const override { return _subscribers.load() > 0 || _peerReady.load(); }
  size_t txSpace() const override;
  void send(const MidiMessage &msg) override;

  bool canScan() const override { return _central; }
  void startScan() override;
  bool scanning() const override { return _scanRequest.load() || _scanning.load(); }
  int scanResults(Found *found, int max) const override;
  void connectTo(const Found &device, RadioConfig &config) override;
  void forget(RadioConfig &config) override;

  // NimBLE callbacks (BLE host task) and the central task: not for other callers
  void onHostWrite(const uint8_t *data, size_t length);
  void onPeerNotify(const uint8_t *data, size_t length);
  void onSubscribe(uint16_t connection, bool subscribed, uint16_t mtu);
  bool subscribed(uint16_t connection) const
  {
    for (int i = 0; i < _subscribers.load(); i++) {
      if (_hosts[i] == connection) return true;
    }
    return false;
  }
  void onHostDisconnect(uint16_t connection) { onSubscribe(connection, false, 0); }
  void onPeerDisconnect();
  void onFound(const char *name, const char *address, uint8_t addressType);
  void onScanEnd() { _scanning = false; }
  void centralTask();

private:
  static void queuePacket(void *context, const uint8_t *packet, size_t length);
  bool transmit(const uint8_t *packet, size_t length);
  bool connectPeer();

  static constexpr int kMaxFound = 8;

  MidiHandler _handler = nullptr;
  bool _central = false;

  // Received (one ring and decoder per side: running status and SysEx belong to one sender)
  PacketRing _rxHost, _rxPeer;
  BleMidiDecoder _decodeHost, _decodePeer;

  // Sent
  BleMidiEncoder _encoder;
  PacketRing _tx;
  uint8_t _retry[BleMidiEncoder::kMaxPacket];
  size_t _retryLength = 0;

  static constexpr int kMaxHosts = 3;
  uint16_t _hosts[kMaxHosts];  ///< connection handles of subscribed hosts (BLE host task only)
  std::atomic<int> _subscribers{0};
  std::atomic<uint16_t> _hostMtu{23};
  std::atomic<bool> _peerReady{false};
  std::atomic<uint16_t> _peerMtu{23};

  // Central: what to connect to (written by the UI, read by the central task)
  char _target[18] = "";
  uint8_t _targetType = 0;
  char _targetName[24] = "";
  std::atomic<bool> _targetChanged{false};
  std::atomic<bool> _connecting{false};
  std::atomic<bool> _scanRequest{false};
  std::atomic<bool> _scanning{false};
  Found _found[kMaxFound];
  int _foundCount = 0;

  // Counters (shown on the Radio page)
  std::atomic<uint32_t> _rxPackets{0};
  uint32_t _txPackets = 0;
  uint32_t _txFailed = 0;
};
