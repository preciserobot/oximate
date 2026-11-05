#include "ble_midi.h"

#if defined(ESP_PLATFORM)
#include <Arduino.h>
#include <NimBLEDevice.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <string.h>

static const char *const kServiceUuid = "03B80E5A-EDE8-4B33-A751-6CE34EC4C700";
static const char *const kCharacteristicUuid = "7772E5DB-3868-4112-A1A9-F2669D106BF3";

static portMUX_TYPE gLock = portMUX_INITIALIZER_UNLOCKED; // _found, _target
static SemaphoreHandle_t gClientLock = nullptr;           // held while the peer's attributes change
static NimBLECharacteristic *gCharacteristic = nullptr;
static NimBLEClient *gClient = nullptr;
static NimBLERemoteCharacteristic *gRemote = nullptr;
static BleMidi *gBle = nullptr;

static void copyText(char *to, size_t size, const char *from)
{
  strncpy(to, from, size - 1);
  to[size - 1] = 0;
}

// ---------------------------------------------------------------- NimBLE callbacks

class ServerCallbacks : public NimBLEServerCallbacks
{
  void onConnect(NimBLEServer *server, NimBLEConnInfo &info) override
  {
    // Low latency: 7.5-15 ms connection interval (the host may refuse)
    server->updateConnParams(info.getConnHandle(), 6, 12, 0, 200);
    if (server->getConnectedCount() < 2) NimBLEDevice::startAdvertising(); // a second host (Mac + iPad)
  }
  void onDisconnect(NimBLEServer *, NimBLEConnInfo &info, int) override { gBle->onHostDisconnect(info.getConnHandle()); }
  void onMTUChange(uint16_t mtu, NimBLEConnInfo &info) override
  {
    if (gBle->subscribed(info.getConnHandle())) gBle->onSubscribe(info.getConnHandle(), true, mtu);
  }
};

class CharacteristicCallbacks : public NimBLECharacteristicCallbacks
{
  void onWrite(NimBLECharacteristic *c, NimBLEConnInfo &) override
  {
    NimBLEAttValue value = c->getValue();
    gBle->onHostWrite(value.data(), value.size());
  }
  void onSubscribe(NimBLECharacteristic *, NimBLEConnInfo &info, uint16_t subValue) override
  {
    gBle->onSubscribe(info.getConnHandle(), subValue != 0, info.getMTU());
  }
};

class ClientCallbacks : public NimBLEClientCallbacks
{
  void onDisconnect(NimBLEClient *, int) override { gBle->onPeerDisconnect(); }
};

class ScanCallbacks : public NimBLEScanCallbacks
{
  void onResult(const NimBLEAdvertisedDevice *device) override
  {
    if (!device->isAdvertisingService(NimBLEUUID(kServiceUuid))) return;
    std::string name = device->getName();
    std::string address = device->getAddress().toString();
    gBle->onFound(name.c_str(), address.c_str(), device->getAddress().getType());
  }
  void onScanEnd(const NimBLEScanResults &, int) override { gBle->onScanEnd(); }
};

static ServerCallbacks gServerCallbacks;
static CharacteristicCallbacks gCharacteristicCallbacks;
static ClientCallbacks gClientCallbacks;
static ScanCallbacks gScanCallbacks;

// ---------------------------------------------------------------- set up

bool BleMidi::begin(const RadioConfig &config, MidiHandler handler)
{
  gBle = this;
  _handler = handler;
  _central = config.central;
  copyText(_target, sizeof(_target), config.peerAddress);
  copyText(_targetName, sizeof(_targetName), config.peerName);
  _targetType = config.peerAddressType;
  gClientLock = xSemaphoreCreateMutex();

  if (!NimBLEDevice::init(kDeviceName)) return false;
  NimBLEDevice::setMTU(BleMidiEncoder::kMaxPacket + 3);

  NimBLEServer *server = NimBLEDevice::createServer();
  server->setCallbacks(&gServerCallbacks, false);
  server->advertiseOnDisconnect(true);
  NimBLEService *service = server->createService(kServiceUuid);
  gCharacteristic = service->createCharacteristic(
      kCharacteristicUuid, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE_NR | NIMBLE_PROPERTY::NOTIFY);
  gCharacteristic->setCallbacks(&gCharacteristicCallbacks);
  gCharacteristic->setValue((const uint8_t *)"", 0); // reads return no payload (BLE MIDI spec)
  service->start();

  NimBLEAdvertising *advertising = NimBLEDevice::getAdvertising();
  advertising->addServiceUUID(kServiceUuid);
  advertising->enableScanResponse(true);
  advertising->setName(kDeviceName);
  advertising->start();

  if (_central) {
    NimBLEScan *scan = NimBLEDevice::getScan();
    scan->setScanCallbacks(&gScanCallbacks, false);
    scan->setActiveScan(true); // names are in the scan responses
    scan->setInterval(100);
    scan->setWindow(60);
    gClient = NimBLEDevice::createClient();
    gClient->setClientCallbacks(&gClientCallbacks, false);
    gClient->setConnectionParams(6, 12, 0, 200);
    gClient->setConnectTimeout(3000);
    _targetChanged = _target[0] != 0;
    xTaskCreatePinnedToCore([](void *self) { static_cast<BleMidi *>(self)->centralTask(); }, "ble-central",
                            4096, this, 2, nullptr, 0);
  }
  return true;
}

// ---------------------------------------------------------------- MIDI task

void BleMidi::send(const MidiMessage &msg)
{
  if (!msg.length || !radioCarries(msg.bytes[0])) return;
  _encoder.add(msg.bytes, msg.length, (uint16_t)millis(), queuePacket, this);
}

void BleMidi::queuePacket(void *context, const uint8_t *packet, size_t length)
{
  static_cast<BleMidi *>(context)->_tx.push(packet, length);
}

size_t BleMidi::txSpace() const
{
  size_t free = _tx.freeBytes();
  return free > 64 ? free - 64 : 0; // headers and timestamps
}

bool BleMidi::transmit(const uint8_t *packet, size_t length)
{
  bool sent = false;
  if (_subscribers.load() > 0 && gCharacteristic) {
    sent = gCharacteristic->notify(packet, length);
  }
  if (_peerReady.load() && gClientLock && xSemaphoreTake(gClientLock, 0) == pdTRUE) {
    if (gRemote && gRemote->writeValue(packet, length, false)) sent = true;
    xSemaphoreGive(gClientLock);
  }
  return sent;
}

void BleMidi::poll()
{
  uint8_t packet[BleMidiEncoder::kMaxPacket + 3];
  size_t length;
  uint32_t now = micros();
  while ((length = _rxHost.pop(packet, sizeof(packet))) > 0) _decodeHost.decode(packet, length, now, _handler);
  while ((length = _rxPeer.pop(packet, sizeof(packet))) > 0) _decodePeer.decode(packet, length, now, _handler);

  // What was sent since the last poll leaves as one packet (or more)
  _encoder.flush(queuePacket, this);
  uint16_t mtu = 0;
  if (_subscribers.load() > 0) mtu = _hostMtu.load();
  if (_peerReady.load() && (!mtu || _peerMtu.load() < mtu)) mtu = _peerMtu.load();
  if (mtu) _encoder.setMaxPacket(mtu - 3);

  if (!connected()) {
    while (_tx.pop(packet, sizeof(packet))) {} // nobody to send to
    _retryLength = 0;
    return;
  }
  // The stack refuses when its buffers are full: keep that packet and try again next poll
  for (int i = 0; i < 8; i++) {
    if (!_retryLength) {
      _retryLength = _tx.pop(_retry, sizeof(_retry));
      if (!_retryLength) break;
    }
    if (!transmit(_retry, _retryLength)) {
      _txFailed++;
      break;
    }
    _txPackets++;
    _retryLength = 0;
  }
}

// ---------------------------------------------------------------- BLE host task

void BleMidi::onHostWrite(const uint8_t *data, size_t length)
{
  _rxHost.push(data, length);
  _rxPackets++;
}

void BleMidi::onPeerNotify(const uint8_t *data, size_t length)
{
  _rxPeer.push(data, length);
  _rxPackets++;
}

void BleMidi::onSubscribe(uint16_t connection, bool subscribed, uint16_t mtu)
{
  int n = _subscribers.load();
  int at = -1;
  for (int i = 0; i < n; i++) {
    if (_hosts[i] == connection) at = i;
  }
  if (subscribed && at < 0 && n < kMaxHosts) {
    _hosts[n++] = connection;
  } else if (!subscribed && at >= 0) {
    _hosts[at] = _hosts[--n];
  }
  // Packets go to every host with one notify: the smallest MTU wins
  uint16_t smallest = 0;
  NimBLEServer *server = NimBLEDevice::getServer();
  for (int i = 0; i < n; i++) {
    uint16_t m = server ? server->getPeerMTU(_hosts[i]) : 23;
    if (_hosts[i] == connection && mtu) m = mtu;
    if (m >= 23 && (!smallest || m < smallest)) smallest = m;
  }
  _hostMtu = smallest ? smallest : 23;
  _subscribers = n;
}

void BleMidi::onPeerDisconnect()
{
  _peerReady = false;
}

void BleMidi::onFound(const char *name, const char *address, uint8_t addressType)
{
  portENTER_CRITICAL(&gLock);
  bool known = false;
  for (int i = 0; i < _foundCount; i++) {
    if (strcmp(_found[i].address, address) == 0) {
      known = true;
      if (name[0]) copyText(_found[i].name, sizeof(_found[i].name), name);
    }
  }
  if (!known && _foundCount < kMaxFound) {
    Found &f = _found[_foundCount++];
    copyText(f.name, sizeof(f.name), name);
    copyText(f.address, sizeof(f.address), address);
    f.addressType = addressType;
  }
  portEXIT_CRITICAL(&gLock);
}

// ---------------------------------------------------------------- central task

bool BleMidi::connectPeer()
{
  char address[18];
  uint8_t type;
  portENTER_CRITICAL(&gLock);
  memcpy(address, _target, sizeof(address));
  type = _targetType;
  portEXIT_CRITICAL(&gLock);
  if (!address[0]) return false;

  xSemaphoreTake(gClientLock, portMAX_DELAY);
  gRemote = nullptr;
  bool ok = gClient->connect(NimBLEAddress(std::string(address), type));
  NimBLERemoteService *service = ok ? gClient->getService(kServiceUuid) : nullptr;
  NimBLERemoteCharacteristic *remote = service ? service->getCharacteristic(kCharacteristicUuid) : nullptr;
  ok = remote && remote->canNotify() &&
       remote->subscribe(true, [](NimBLERemoteCharacteristic *, uint8_t *data, size_t length, bool) {
         gBle->onPeerNotify(data, length);
       });
  if (ok) {
    gRemote = remote;
    _peerMtu = gClient->getMTU();
    _decodePeer.reset();
    _peerReady = true;
  } else if (gClient->isConnected()) {
    gClient->disconnect();
  }
  xSemaphoreGive(gClientLock);
  return ok;
}

void BleMidi::centralTask()
{
  uint32_t nextAttempt = 0;
  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(200));
    if (_targetChanged.exchange(false)) {
      if (gClient->isConnected()) gClient->disconnect(); // a different (or no) device now
      _peerReady = false;
      nextAttempt = 0;
    }
    if (_scanRequest.exchange(false)) {
      NimBLEScan *scan = NimBLEDevice::getScan();
      portENTER_CRITICAL(&gLock);
      _foundCount = 0;
      portEXIT_CRITICAL(&gLock);
      scan->clearResults();
      _scanning = scan->start(5000, false, true);
    }
    if (_scanning.load() || _peerReady.load() || gClient->isConnected()) continue;
    if ((int32_t)(millis() - nextAttempt) < 0) continue;
    _connecting = true;
    bool ok = connectPeer();
    _connecting = false;
    nextAttempt = millis() + (ok ? 0 : 3000); // retry while the device is away
  }
}

// ---------------------------------------------------------------- UI thread

void BleMidi::startScan()
{
  if (_central) _scanRequest = true;
}

int BleMidi::scanResults(Found *found, int max) const
{
  portENTER_CRITICAL(&gLock);
  int n = _foundCount < max ? _foundCount : max;
  memcpy(found, _found, n * sizeof(Found));
  portEXIT_CRITICAL(&gLock);
  return n;
}

void BleMidi::connectTo(const Found &device, RadioConfig &config)
{
  copyText(config.peerAddress, sizeof(config.peerAddress), device.address);
  copyText(config.peerName, sizeof(config.peerName), device.name[0] ? device.name : device.address);
  config.peerAddressType = device.addressType;
  portENTER_CRITICAL(&gLock);
  copyText(_target, sizeof(_target), config.peerAddress);
  copyText(_targetName, sizeof(_targetName), config.peerName);
  _targetType = device.addressType;
  portEXIT_CRITICAL(&gLock);
  _targetChanged = true;
}

void BleMidi::forget(RadioConfig &config)
{
  config.peerAddress[0] = 0;
  config.peerName[0] = 0;
  config.peerAddressType = 0;
  portENTER_CRITICAL(&gLock);
  _target[0] = 0;
  _targetName[0] = 0;
  portEXIT_CRITICAL(&gLock);
  _targetChanged = true;
}

void BleMidi::status(char lines[kStatusLines][kStatusLength]) const
{
  int hosts = _subscribers.load();
  if (hosts) {
    snprintf(lines[0], kStatusLength, "\"%s\": %d host%s connected", kDeviceName, hosts, hosts == 1 ? "" : "s");
  } else {
    snprintf(lines[0], kStatusLength, "\"%s\": advertising", kDeviceName);
  }
  char name[24];
  portENTER_CRITICAL(&gLock);
  memcpy(name, _targetName, sizeof(name));
  portEXIT_CRITICAL(&gLock);
  if (!_central) {
    snprintf(lines[1], kStatusLength, "Central off");
  } else if (!name[0]) {
    snprintf(lines[1], kStatusLength, "Central: no device (B: Devices)");
  } else {
    const char *state = _peerReady.load() ? "connected" : _connecting.load() ? "connecting" : "waiting";
    snprintf(lines[1], kStatusLength, "%.20s: %s", name, state);
  }
  snprintf(lines[2], kStatusLength, "rx %lu tx %lu fail %lu drop %lu", (unsigned long)_rxPackets.load(),
           (unsigned long)_txPackets, (unsigned long)_txFailed,
           (unsigned long)(_rxHost.dropped() + _rxPeer.dropped() + _tx.dropped()));
}

#endif // ESP_PLATFORM
