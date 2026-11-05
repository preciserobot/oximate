#include "net_midi.h"

#if defined(ESP_PLATFORM) && defined(OXIMATE_WIFI) // the Wi-Fi firmware (see radio.h)
#include <Arduino.h>
#include <ESPmDNS.h>
#include <WebServer.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <esp_random.h>
#include <esp_timer.h>
#include <esp_wifi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <string.h>

static NetMidi *gNet = nullptr;
static WiFiUDP gControl, gData;
static WebServer *gWeb = nullptr;
static portMUX_TYPE gLock = portMUX_INITIALIZER_UNLOCKED; // _peerName

static void copyText(char *to, size_t size, const char *from)
{
  strncpy(to, from, size - 1);
  to[size - 1] = 0;
}

static void received(const MidiMessage &msg) { gNet->onReceived(msg); }

static void sender(void *, bool dataPort, AppleMidiSession::Endpoint to, const uint8_t *data, size_t length)
{
  gNet->sendUdp(dataPort, to, data, length);
}

static void sendPacket(void *context, const uint8_t *packet, size_t length)
{
  static_cast<AppleMidiSession *>(context)->sendData(packet, length);
}

static uint64_t rtpClock() { return esp_timer_get_time() / 100; } // 100 us units (RTP-MIDI 10 kHz)

// ---------------------------------------------------------------- set up

bool NetMidi::begin(const RadioConfig &config, MidiHandler handler)
{
  gNet = this;
  _handler = handler;
  _config = config;

  // Access point name and password from the MAC address: stable, and different per device
  uint8_t mac[6];
  WiFi.macAddress(mac);
  uint32_t hash = 2166136261u;
  for (uint8_t b : mac) hash = (hash ^ b) * 16777619u;
  snprintf(_apName, sizeof(_apName), "OxiMate-%02X%02X", mac[4], mac[5]);
  snprintf(_apPassword, sizeof(_apPassword), "oxi-%08lx", (unsigned long)hash);

  WiFi.persistent(false); // credentials live in OxiMate's own settings
  bool join = config.joinNetwork && config.ssid[0];
  if (join) {
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);
    WiFi.begin(config.ssid, config.password);
  } else {
    startAccessPoint();
  }
  esp_wifi_set_ps(WIFI_PS_NONE); // power save off while network MIDI runs (decided 2026-11-05)

  _session.begin(kName, esp_random(), sender, this);
  _encoder.begin(_session.ssrc());
  xTaskCreatePinnedToCore([](void *self) { static_cast<NetMidi *>(self)->networkTask(); }, "net-midi", 8192,
                          this, 5, nullptr, 0);
  return true;
}

void NetMidi::startAccessPoint()
{
  WiFi.mode(WIFI_AP_STA); // STA for the setup page's network scan
  WiFi.setSleep(false);
  WiFi.softAP(_apName, _apPassword);
  _accessPoint = true;
}

void NetMidi::startServices()
{
  IPAddress ip = _accessPoint.load() ? WiFi.softAPIP() : WiFi.localIP();
  _ip = (uint32_t)ip;
  gControl.begin(RtpMidi::kControlPort);
  gData.begin(RtpMidi::kControlPort + 1);
  if (MDNS.begin("oximate")) {
    MDNS.setInstanceName(kName);
    MDNS.addService("apple-midi", "udp", RtpMidi::kControlPort);
  }
  if (_accessPoint.load()) serveSetup();
  _online = true;
}

// ---------------------------------------------------------------- setup page (own access point)

static String escape(const String &s)
{
  String out;
  for (char c : s) {
    if (c == '<') out += "&lt;";
    else if (c == '>') out += "&gt;";
    else if (c == '&') out += "&amp;";
    else if (c == '"') out += "&quot;";
    else out += c;
  }
  return out;
}

void NetMidi::serveSetup()
{
  gWeb = new WebServer(80);
  gWeb->on("/", HTTP_GET, [] {
    String page = "<!DOCTYPE html><html><head><meta name=viewport content='width=device-width'>"
                  "<title>OxiMate Wi-Fi</title></head><body style='font-family:sans-serif'>"
                  "<h2>OxiMate: join a network</h2><form method=post action=/save>"
                  "<p>Network<br><input name=ssid list=nets maxlength=32 required></p><datalist id=nets>";
    int n = WiFi.scanNetworks(); // a few seconds; only while someone uses this page
    for (int i = 0; i < n && i < 20; i++) page += "<option value=\"" + escape(WiFi.SSID(i)) + "\">";
    WiFi.scanDelete();
    page += "</datalist><p>Password<br><input name=password type=password maxlength=64></p>"
            "<p><button>Save and reboot</button></p></form>"
            "<p>OxiMate then joins this network; if it can't, it opens its own again.</p></body></html>";
    gWeb->send(200, "text/html", page);
  });
  gWeb->on("/save", HTTP_POST, [] {
    String ssid = gWeb->arg("ssid"), password = gWeb->arg("password");
    if (!ssid.length() || ssid.length() > 32 || password.length() > 64) {
      gWeb->send(400, "text/plain", "Network name 1-32 characters, password up to 64");
      return;
    }
    RadioConfig config = RadioSettings::load();
    copyText(config.ssid, sizeof(config.ssid), ssid.c_str());
    copyText(config.password, sizeof(config.password), password.c_str());
    config.joinNetwork = true;
    config.mode = RadioMode::Wifi;
    RadioSettings::save(config);
    gWeb->send(200, "text/html",
               "<!DOCTYPE html><html><body style='font-family:sans-serif'><h2>Saved</h2>"
               "<p>OxiMate reboots and joins " + escape(ssid) + ".</p></body></html>");
    gNet->scheduleReboot();
  });
  gWeb->onNotFound([] { gWeb->send(404, "text/plain", "OxiMate: open http://192.168.4.1/"); });
  gWeb->begin();
}

// ---------------------------------------------------------------- network task

void NetMidi::sendUdp(bool dataPort, AppleMidiSession::Endpoint to, const uint8_t *data, size_t length)
{
  WiFiUDP &udp = dataPort ? gData : gControl;
  udp.beginPacket(IPAddress(to.ip), to.port);
  udp.write(data, length);
  udp.endPacket();
}

void NetMidi::onReceived(const MidiMessage &msg)
{
  if (radioCarries(msg.bytes[0])) _rx.push(msg.bytes, msg.length); // no clock over radio
}

void NetMidi::networkTask()
{
  uint8_t *packet = _packet; // one UDP datagram (a member: NetMidi is on the heap)
  uint32_t joinStart = millis();
  for (;;) {
    uint32_t now = millis();
    if (!_online.load()) {
      if (_accessPoint.load() || WiFi.status() == WL_CONNECTED) {
        startServices();
      } else if (now - joinStart > kJoinTimeoutMillis) {
        _joinFailed = true; // open the own access point again, so the setup page is reachable
        WiFi.disconnect();
        startAccessPoint();
      }
      vTaskDelay(pdMS_TO_TICKS(50));
      continue;
    }

    int length;
    while ((length = gControl.parsePacket()) > 0) {
      int n = gControl.read(packet, kPacketBytes);
      AppleMidiSession::Endpoint from{(uint32_t)gControl.remoteIP(), gControl.remotePort()};
      if (n > 0) _session.onControl(packet, n, from, rtpClock(), millis());
    }
    while ((length = gData.parsePacket()) > 0) {
      int n = gData.read(packet, kPacketBytes);
      AppleMidiSession::Endpoint from{(uint32_t)gData.remoteIP(), gData.remotePort()};
      if (n > 0) {
        uint32_t before = _session.receivedPackets();
        _session.onData(packet, n, from, rtpClock(), millis(), micros(), received);
        if (_session.receivedPackets() != before) _rxPackets++;
      }
    }
    _session.poll(millis());

    // What the MIDI task sent: one RTP packet per round (more for long SysEx)
    if (_session.connected()) {
      uint32_t timestamp = (uint32_t)rtpClock();
      uint16_t before = _encoder.sequence();
      while ((length = _tx.pop(packet, kPacketBytes)) > 0) {
        _encoder.add(packet, length, timestamp, sendPacket, &_session);
      }
      _encoder.flush(sendPacket, &_session);
      _txPackets += (uint16_t)(_encoder.sequence() - before);
    } else {
      while (_tx.pop(packet, kPacketBytes) > 0) {} // nobody to send to
    }

    bool connected = _session.connected();
    if (connected != _connected.load()) {
      portENTER_CRITICAL(&gLock);
      copyText(_peerName, sizeof(_peerName), _session.peerName());
      portEXIT_CRITICAL(&gLock);
      _connected = connected;
    }
    _latency = _session.latency100us();
    _lost = _session.lostPackets();

    if (gWeb) gWeb->handleClient();
    if (_rebootAt && (int32_t)(millis() - _rebootAt) >= 0) ESP.restart();
    vTaskDelay(1); // 1 ms: incoming packets wait at most this long, outgoing are batched per round
  }
}

void NetMidi::scheduleReboot() { _rebootAt = millis() + 1500; }

// ---------------------------------------------------------------- MIDI task

void NetMidi::poll()
{
  size_t length;
  while ((length = _rx.pop(_message, sizeof(_message))) > 0) {
    MidiMessage msg{MidiSource::Net, 0, _message, (uint16_t)length};
    msg.timeMicros = micros();
    if (_handler) _handler(msg);
  }
}

size_t NetMidi::txSpace() const
{
  size_t free = _tx.freeBytes();
  return free > 2 ? free - 2 : 0;
}

void NetMidi::send(const MidiMessage &msg)
{
  if (!msg.length || !radioCarries(msg.bytes[0]) || !_connected.load()) return;
  _tx.push(msg.bytes, msg.length);
}

// ---------------------------------------------------------------- UI thread

void NetMidi::status(char lines[kStatusLines][kStatusLength]) const
{
  IPAddress ip(_ip.load());
  if (_accessPoint.load()) {
    snprintf(lines[0], kStatusLength, "AP %s  pw %s", _apName, _apPassword);
    if (_joinFailed.load()) {
      snprintf(lines[1], kStatusLength, "Join %.12s failed; setup %s", _config.ssid, ip.toString().c_str());
    } else {
      snprintf(lines[1], kStatusLength, "%s  setup: http://%s", ip.toString().c_str(), ip.toString().c_str());
    }
  } else if (_online.load()) {
    snprintf(lines[0], kStatusLength, "Joined %.32s", _config.ssid);
    snprintf(lines[1], kStatusLength, "%s  mDNS oximate.local", ip.toString().c_str());
  } else {
    snprintf(lines[0], kStatusLength, "Joining %.32s ...", _config.ssid);
    lines[1][0] = 0;
  }
  if (_connected.load()) {
    char name[24];
    portENTER_CRITICAL(&gLock);
    memcpy(name, _peerName, sizeof(name));
    portEXIT_CRITICAL(&gLock);
    uint32_t latency = _latency.load();
    snprintf(lines[2], kStatusLength, "%.14s %lu.%lums rx%lu tx%lu lost%lu", name[0] ? name : "Session",
             (unsigned long)(latency / 10), (unsigned long)(latency % 10), (unsigned long)_rxPackets.load(),
             (unsigned long)_txPackets.load(), (unsigned long)_lost.load());
  } else {
    copyText(lines[2], kStatusLength, _online.load() ? "No session: connect from the Mac/iPad" : "");
  }
}

#endif // ESP_PLATFORM && OXIMATE_WIFI
