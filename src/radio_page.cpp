#include "radio_page.h"

using namespace Layout;

static constexpr int kModeY = 24, kChipH = 26;
static constexpr int kInfoY = 54;
static constexpr int kStatusY = 74;
static constexpr int kRoutesY = 128;
static constexpr int kOptionsY = 160;
static constexpr int kListY = 40, kListH = 22; // scan results, below the heading line
static constexpr uint32_t kRedrawMillis = 500;

static const char *const kModeNames[] = {"Off", "BLE", "Wi-Fi"};

void RadioPage::buttonLabels(const char *labels[3]) const
{
  if (_scanView) {
    labels[0] = "";
    labels[1] = _transport && _transport->scanning() ? "Scanning" : "Scan";
    labels[2] = "Back";
    return;
  }
  labels[0] = _config.mode != _running ? "Reboot" : "";
  if (_running == RadioMode::Ble) {
    labels[1] = _config.central ? "Devices" : "";
    labels[2] = _config.peerAddress[0] ? "Forget" : "";
  } else {
    labels[1] = "";
    labels[2] = "";
  }
}

void RadioPage::onButton(int index)
{
  if (_scanView) {
    if (index == 1 && _transport) _transport->startScan();
    if (index == 2) _scanView = false;
    markDirty();
    return;
  }
  switch (index) {
    case 0:
      if (_config.mode != _running) {
        RadioSettings::save(_config);
#if defined(ESP_PLATFORM)
        if (ui()) ui()->flash("Rebooting", TFT_DARKGREY, 2000);
        delay(300);
        ESP.restart(); // radio stacks are chosen at boot (decided 2026-11-05)
#endif
      }
      break;
    case 1:
      if (_running == RadioMode::Ble && _config.central && _transport && _transport->canScan()) {
        _scanView = true;
        _transport->startScan();
        markDirty();
      }
      break;
    case 2:
      if (_running == RadioMode::Ble && _transport && _config.peerAddress[0]) {
        _transport->forget(_config);
        RadioSettings::save(_config);
        markDirty();
      }
      break;
  }
}

void RadioPage::onTouch(int x, int y)
{
  if (_scanView) {
    int index = (y - kListY) / kListH;
    if (y >= kListY && index >= 0 && index < _foundCount && _transport) {
      _transport->connectTo(_found[index], _config);
      RadioSettings::save(_config);
      _scanView = false;
      markDirty();
    }
    return;
  }
  if (y >= kModeY && y < kModeY + kChipH) {
    int mode = x / (320 / 3);
    _config.mode = (RadioMode)(mode > 2 ? 2 : mode);
    RadioSettings::save(_config); // applied at the next boot (A: Reboot)
  } else if (_running != RadioMode::Off && y >= kRoutesY && y < kRoutesY + kChipH) {
    RouterConfig &c = _router.config();
    MidiSource in = radioInput();
    MidiOutput out = radioOutput();
    switch (x / 80) {
      case 0: c.setRouted(in, MidiOutput::Din, !c.routed(in, MidiOutput::Din)); break;
      case 1: c.setRouted(in, MidiOutput::Usb, !c.routed(in, MidiOutput::Usb)); break;
      case 2: c.setRouted(MidiSource::Din, out, !c.routed(MidiSource::Din, out)); break;
      default: c.setRouted(MidiSource::Usb, out, !c.routed(MidiSource::Usb, out)); break;
    }
    _router.changed();
  } else if (y >= kOptionsY && y < kOptionsY + kChipH) {
    if (_running == RadioMode::Ble || _config.mode == RadioMode::Ble) {
      _config.central = !_config.central; // central mode starts at the next boot
    } else {
      _config.joinNetwork = !_config.joinNetwork; // own access point or join, at the next boot
    }
    RadioSettings::save(_config);
  } else {
    return;
  }
  markDirty();
}

void RadioPage::tick()
{
  // Status (connections, scan results) changes on its own
  if (millis() - _lastDraw >= kRedrawMillis) {
    _lastDraw = millis();
    if (_scanView && _transport) _foundCount = _transport->scanResults(_found, 8);
    markDirty();
    markFooterDirty();
  }
}

void RadioPage::draw()
{
  canvas().fillRect(0, kContentTop, canvas().width(), kContentHeight, TFT_BLACK);
  if (_scanView) {
    drawScan();
  } else {
    drawMain();
  }
}

void RadioPage::drawMain()
{
  M5Canvas &c = canvas();
  for (int m = 0; m < 3; m++) {
    drawChip(c, m * (320 / 3), kModeY, 320 / 3, kChipH, kModeNames[m], (int)_config.mode == m);
  }
  char text[48];
  if (_config.mode != _running) {
    snprintf(text, sizeof(text), "Running: %s - A reboots into %s", kModeNames[(int)_running],
             kModeNames[(int)_config.mode]);
    c.setTextColor(TFT_YELLOW, TFT_BLACK);
  } else {
    snprintf(text, sizeof(text), "Running: %s  (MIDI task on core %d)", kModeNames[(int)_running],
             _running == RadioMode::Off ? 0 : 1);
    c.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  }
  c.setCursor(2, kInfoY);
  c.print(text);

  if (_running == RadioMode::Off || !_transport) {
    c.setTextColor(TFT_DARKGREY, TFT_BLACK);
    c.setCursor(2, kStatusY);
    c.print("No radio: USB and DIN only.");
    c.setCursor(2, kStatusY + kCharHeight);
    c.print("Pick BLE or Wi-Fi, then A to reboot.");
    return;
  }

  char lines[RadioTransport::kStatusLines][RadioTransport::kStatusLength] = {};
  _transport->status(lines);
  c.setTextColor(sourceColor(radioInput()), TFT_BLACK);
  for (int i = 0; i < RadioTransport::kStatusLines; i++) {
    c.setCursor(2, kStatusY + i * kCharHeight);
    c.print(lines[i]);
  }

  // Routes of the running radio (the full matrix is in RouterConfig::routes)
  const RouterConfig &rc = _router.config();
  const char *name = _running == RadioMode::Ble ? "BLE" : "NET";
  MidiSource in = radioInput();
  MidiOutput out = radioOutput();
  char labels[4][10];
  snprintf(labels[0], sizeof(labels[0]), "%s>DIN", name);
  snprintf(labels[1], sizeof(labels[1]), "%s>USB", name);
  snprintf(labels[2], sizeof(labels[2]), "DIN>%s", name);
  snprintf(labels[3], sizeof(labels[3]), "USB>%s", name);
  bool on[4] = {rc.routed(in, MidiOutput::Din), rc.routed(in, MidiOutput::Usb), rc.routed(MidiSource::Din, out),
                rc.routed(MidiSource::Usb, out)};
  for (int i = 0; i < 4; i++) drawChip(c, i * 80, kRoutesY, 80, kChipH, labels[i], on[i]);

  if (_running == RadioMode::Ble) {
    drawChip(c, 0, kOptionsY, 160, kChipH, _config.central ? "Central: on" : "Central: off", _config.central);
    c.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    c.setCursor(166, kOptionsY + 5);
    c.print(_config.peerName[0] ? _config.peerName : "(no device)");
  } else {
    drawChip(c, 0, kOptionsY, 200, kChipH, _config.joinNetwork ? "Join network" : "Own access point",
             _config.joinNetwork);
  }
  c.setFont(&fonts::Font0);
  c.setTextColor(TFT_DARKGREY, TFT_BLACK);
  c.setCursor(2, kOptionsY + kChipH + 6);
  c.print(_running == RadioMode::Ble ? "Central mode and device changes apply at the next boot."
                                     : "Network choice applies at the next boot. No clock over radio.");
  c.setFont(&fonts::AsciiFont8x16);
}

void RadioPage::drawScan()
{
  M5Canvas &c = canvas();
  c.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  c.setCursor(2, kContentTop + 2);
  c.print(_foundCount ? "BLE MIDI devices (tap to connect):" : "Searching for BLE MIDI devices...");
  for (int i = 0; i < _foundCount; i++) {
    char text[48];
    snprintf(text, sizeof(text), "%-22s %s", _found[i].name[0] ? _found[i].name : "(no name)", _found[i].address);
    drawChip(c, 0, kListY + i * kListH, 320, kListH - 2, text, strcmp(_found[i].address, _config.peerAddress) == 0);
  }
}
