// Tests for the radio groundwork: more inputs and outputs, the route matrix, migration
#include <Arduino.h> // fake millis()/micros()
#include "test_support.h"
#include "panic.h"

/*! A radio peer that records what is sent to it */
struct FakeRadio : RadioPort
{
  bool up = true;
  std::vector<std::vector<uint8_t>> sent;
  bool connected() const override { return up; }
  size_t txSpace() const override { return 512; }
  void send(const MidiMessage &msg) override { sent.push_back(std::vector<uint8_t>(msg.bytes, msg.bytes + msg.length)); }
};

TEST(testRadioRouting)
{
  OutputRig rig;
  FakeRadio ble, net;
  rig.outputs.setPort(MidiOutput::Ble, &ble);
  rig.outputs.setPort(MidiOutput::Net, &net);
  rig.usb.poll(); // the fake USB device is connected

  // BLE input goes to DIN and USB by default, never back to BLE
  MidiMessage fromBle = msg(MidiSource::Ble, {0x91, 60, 100});
  CHECK(rig.router.route(fromBle));
  CHECK(rig.dinBytes() == std::vector<uint8_t>({0x91, 60, 100}));
  CHECK(ble.sent.empty() && net.sent.empty());
  CHECK(rig.tracker.isHeld(MidiOutput::Usb, 1, 60));

  // DIN input also reaches both radio peers
  gFakeMicros += 100000;
  CHECK(rig.router.route(msg(MidiSource::Din, {0x92, 61, 90})));
  CHECK(ble.sent.size() == 1 && ble.sent[0] == std::vector<uint8_t>({0x92, 61, 90}));
  CHECK(net.sent.size() == 1);

  // A disconnected peer gets nothing (and nothing is tracked for it)
  ble.up = false;
  CHECK(rig.router.route(msg(MidiSource::Usb, {0x93, 62, 80})));
  CHECK(ble.sent.size() == 1 && net.sent.size() == 2);
  CHECK(!rig.tracker.isHeld(MidiOutput::Ble, 3, 62));

  // No clock over radio: clock and transport from BLE are dropped (the engine doesn't follow it)
  uint8_t tick = 0xF8;
  CHECK(!rig.router.route({MidiSource::Ble, 0, &tick, 1}));

  // The matrix can switch a route off
  rig.router.config().setRouted(MidiSource::Net, MidiOutput::Din, false);
  rig.router.changed();
  rig.dinBytes();
  rig.router.route(msg(MidiSource::Net, {0x94, 63, 70}));
  CHECK(rig.dinBytes().empty());
}

TEST(testRadioPeerGoneReleasesNotes)
{
  OutputRig rig;
  FakeRadio ble;
  rig.outputs.setPort(MidiOutput::Ble, &ble);
  Panic panic;
  panic.begin(rig.outputs, rig.tracker, rig.usb, rig.clock);
  gFakeMillis = 1000;
  panic.run(gFakeMillis); // sees the peer connected
  rig.router.route(msg(MidiSource::Ble, {0x90, 64, 100}));
  rig.dinBytes();
  CHECK(rig.tracker.isHeld(MidiOutput::Din, 0, 64));
  ble.up = false; // the iPad went away mid-note
  panic.run(gFakeMillis);
  CHECK(rig.dinBytes() == std::vector<uint8_t>({0x80, 64, 0}));
  CHECK(!rig.tracker.isHeld(MidiOutput::Din, 0, 64));
}

TEST(testRouterMigrationV2)
{
  gFakeNvs.clear();
  struct
  {
    uint8_t version;
    bool usbToDin, dinToDin, dinToUsb;
    uint16_t channelMask[2];
    uint8_t forceChannel[2];
    bool sysex;
    uint8_t ledLevel;
  } v2;
  memset(&v2, 0, sizeof(v2));
  v2 = {2, false, true, false, {0x00FF, 0xFFFF}, {0, 4}, false, 1};
  gFakeNvs["oximate/router"] = std::vector<uint8_t>((uint8_t *)&v2, (uint8_t *)&v2 + sizeof(v2));

  OutputRig rig; // clears NVS: set it again and load a fresh router
  gFakeNvs["oximate/router"] = std::vector<uint8_t>((uint8_t *)&v2, (uint8_t *)&v2 + sizeof(v2));
  Router router;
  router.begin(rig.outputs, rig.clock);
  const RouterConfig &c = router.config();
  CHECK(c.version == 3);
  CHECK(!c.routed(MidiSource::Usb, MidiOutput::Din) && c.routed(MidiSource::Din, MidiOutput::Din) &&
        !c.routed(MidiSource::Din, MidiOutput::Usb));
  CHECK(c.channelMask[0] == 0x00FF && c.forceChannel[1] == 4 && !c.sysex && c.ledLevel == 1);
  // Radio inputs get the defaults
  CHECK(c.routed(MidiSource::Ble, MidiOutput::Din) && c.routed(MidiSource::Net, MidiOutput::Usb));
  CHECK(c.channelMask[2] == 0xFFFF && c.forceChannel[3] == 0);
  ClockSource unused;
  CHECK(!router.migratedClock(unused)); // only version 1 carried a clock setting
}

TEST(testOriginAndNames)
{
  CHECK(Origin::fromInput(MidiSource::Ble).kind == Origin::BleIn);
  CHECK(Origin::fromInput(MidiSource::Net).kind == Origin::NetIn);
  CHECK(Origin::fromInput(MidiSource::Usb).kind == Origin::UsbIn);
  CHECK(strcmp(sourceName(MidiSource::Ble), "BLE") == 0 && strcmp(sourceName(MidiSource::Net), "NET") == 0);
  CHECK(isWired(MidiSource::Din) && !isWired(MidiSource::Ble));
}
