#pragma once

#include "midi_message.h"

class DinMidi;
class UsbMidi;
class NoteTracker;

enum class MidiOutput : uint8_t
{
  Din = 0, ///< DIN OUT of the Unit MIDI (+ SAM2695)
  Usb = 1, ///< USB device on the host module
  Ble = 2, ///< Bluetooth LE MIDI peer (stage 17)
  Net = 3, ///< RTP-MIDI session (stage 18)
};

/*! Number of MidiOutput values: size of every per-output array */
constexpr int kOutputs = 4;

/*! A radio transport (BLE, network) as seen by the send path. Its send() is called in the MIDI
 *  task and must not block: queue the message and transmit from the transport's own code. */
class RadioPort
{
public:
  virtual ~RadioPort() = default;
  virtual bool connected() const = 0;
  virtual size_t txSpace() const = 0; ///< bytes free for messages
  virtual void send(const MidiMessage &msg) = 0;
};

/*! Who caused a sent message: an input passed through the router, or an internal generator */
struct Origin
{
  enum Kind : uint8_t
  {
    UsbIn,
    DinIn,
    Internal,
    BleIn,
    NetIn,
  };
  Kind kind;
  uint8_t owner; ///< stage number for Internal (e.g. 5 = drum machine), 0 otherwise

  static Origin fromInput(MidiSource source)
  {
    static const Kind kKinds[kInputs] = {UsbIn, DinIn, BleIn, NetIn};
    return {kKinds[(int)source & 3], 0};
  }
  static Origin internal(uint8_t owner) { return {Internal, owner}; }
  bool operator==(const Origin &other) const { return kind == other.kind && owner == other.owner; }
};

/*! The single send path: the router and every generator send through here, so the
 *  note tracker and counters see exactly what went out. Runs in the MIDI task only. */
class MidiOutputs
{
public:
  struct Counters
  {
    uint32_t messages[kOutputs] = {}; ///< per MidiOutput
    uint32_t bytes[kOutputs] = {};
    uint32_t echoesBlocked = 0;    ///< DIN input dropped as an echo of our own DIN output
  };

  static constexpr uint32_t kEchoWindowMicros = 50000;

  /*! Called for every message actually sent (MIDI task) */
  using Listener = void (*)(MidiOutput out, const MidiMessage &msg, Origin origin, void *ctx);
  static constexpr int kMaxListeners = 4;

  void begin(DinMidi &din, UsbMidi &usb, NoteTracker &tracker);
  void addListener(Listener listener, void *ctx);
  /*! Attach a radio transport to MidiOutput::Ble or ::Net (at boot, before the MIDI task) */
  void setPort(MidiOutput out, RadioPort *port);
  void send(MidiOutput out, const MidiMessage &msg, Origin origin);
  bool available(MidiOutput out) const; ///< false while no USB device / radio peer is connected
  bool hasSpace(MidiOutput out, uint16_t length) const; ///< a message of `length` bytes fits now
  NoteTracker &tracker() { return *_tracker; }

  /*! True if `msg` (from DIN IN) is a copy of a channel message we sent to DIN OUT within
   *  kEchoWindowMicros: a device echoing our output back (MIDI thru), which would otherwise
   *  loop forever through the software thru. Each sent message can match once. */
  bool isEcho(const MidiMessage &msg, uint32_t nowMicros);

  const Counters &counters() const { return _counters; }

private:
  DinMidi *_din = nullptr;
  UsbMidi *_usb = nullptr;
  NoteTracker *_tracker = nullptr;
  RadioPort *_ports[kOutputs] = {};
  Counters _counters;
  Listener _listeners[kMaxListeners];
  void *_listenerCtx[kMaxListeners];
  int _listenerCount = 0;

  struct Sent
  {
    uint32_t timeMicros;
    uint8_t length; // 0 = free
    uint8_t bytes[3];
  };
  static constexpr int kSentHistory = 32;
  Sent _sent[kSentHistory] = {};
  int _sentNext = 0;
};
