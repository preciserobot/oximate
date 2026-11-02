#include "scene_page.h"

using namespace Layout;

// Live view
static constexpr int kNowY = 26, kNowH = 40;
static constexpr int kNextY = 70, kNextH = 20;
static constexpr int kCountdownY = 94;
static constexpr int kFollowingY = 122;
static constexpr int kLiveChipsY = 194, kChipH = 24;

// Editor
static constexpr int kEditTopY = 24;
static constexpr int kEditRowsY = 66, kEditRowH = 18, kEditRows = 6;
static constexpr int kEditSummaryY = 176;
static constexpr int kEditChipsY = 196;
static constexpr int kPcX = 120, kPcW = 96, kCcX = 216, kCcW = 104;

// Devices
static constexpr int kDevRowsY = 26, kDevRowH = 21;
static constexpr int kDevNameW = 96, kDevOutX = 96, kDevOutW = 44, kDevChX = 140, kDevChW = 48;
static constexpr int kDevPcX = 188, kDevPcW = 60, kDevLeadX = 248, kDevLeadW = 40, kDevNumX = 288, kDevNumW = 32;

static constexpr uint32_t kRedrawMillis = 100;
static const char *const kTimingNames[] = {"now", "next bar", "next phrase"};

void ScenePage::sceneLabel(int index, char *out, size_t size) const
{
  if (index < 0 || index >= _scenes.count()) {
    snprintf(out, size, "--");
    return;
  }
  Scene &scene = const_cast<Scenes &>(_scenes).scene(index);
  snprintf(out, size, "%02d %s", index + 1, Scenes::kSceneNames[scene.name % Scenes::kSceneNameCount]);
}

void ScenePage::message(const char *text)
{
  if (ui()) ui()->flash(text, TFT_DARKGREY, 1500);
}

void ScenePage::buttonLabels(const char *labels[3]) const
{
  if (_view == View::Live) {
    labels[0] = "Prev";
    labels[1] = "Recall";
    labels[2] = "Next";
  } else {
    labels[0] = "-";
    labels[1] = "Done";
    labels[2] = "+";
  }
}

void ScenePage::enter()
{
  Page::enter();
  _visible = true;
  _leds.claim(kLedOwner, 0, Leds::kCount);
}

void ScenePage::leave()
{
  _visible = false;
  _leds.release(kLedOwner);
  if (_scenes.learning() >= 0) _scenes.setLearn(-1);
}

void ScenePage::onButton(int index)
{
  if (_view == View::Live) {
    switch (index) {
      case 0: _scenes.setArmed(_scenes.armed() - 1); break;
      case 1: _scenes.recall(_scenes.armed()); break;
      case 2: _scenes.setArmed(_scenes.armed() + 1); break;
    }
    markDirty();
    return;
  }
  if (index == 1) { // Done
    if (_scenes.learning() >= 0) _scenes.setLearn(-1);
    _view = View::Live;
    _selectedRow = _selectedCol = -1;
    markDirty();
    return;
  }
  adjust(index == 0 ? -1 : +1);
}

void ScenePage::onButtonHold(int index)
{
  if (_view == View::Live && index == 1) {
    _scenes.recall(_scenes.armed(), true); // hold B: now, ignoring the scene's timing
    markDirty();
  }
}

void ScenePage::adjust(int direction)
{
  if (_view == View::Editor && _selectedRow >= 0 && _selectedCol == 0) {
    Scene &scene = _scenes.scene(_editScene);
    int program = scene.devices[_selectedRow].program + direction;
    if (program < -1) program = 127;
    if (program > 127) program = -1;
    if (!_scenes.setProgram(_editScene, _selectedRow, program)) {
      message("PC not allowed: Devices");
    }
  } else if (_view == View::Devices && _selectedRow >= 0) {
    SceneDevice &device = _scenes.device(_selectedRow);
    if (_selectedCol == 2) device.channel = (device.channel + 17 + direction) % 17;
    else if (_selectedCol == 4) device.leadBeats = (device.leadBeats + 5 + direction) % 5;
    else return;
    _scenes.changed(-1);
  } else {
    return;
  }
  markDirty();
}

void ScenePage::onTouch(int x, int y)
{
  if (_view == View::Editor) {
    editorTouch(x, y);
    return;
  }
  if (_view == View::Devices) {
    devicesTouch(x, y);
    return;
  }
  if (y >= kNowY && y < kNowY + kNowH) {
    _editScene = _scenes.current() >= 0 ? _scenes.current() : _scenes.armed();
    _view = View::Editor;
  } else if (y >= kNextY && y < kNextY + kNextH) {
    _editScene = _scenes.armed();
    _view = View::Editor;
  } else if (y >= kLiveChipsY && y < kLiveChipsY + kChipH) {
    if (x < 107) {
      _view = View::Devices;
    } else if (x < 214) {
      int index = _scenes.addScene(_scenes.count() - 1);
      if (index < 0) {
        message("Setlist full (32)");
      } else {
        _editScene = index;
        _view = View::Editor;
      }
    } else {
      _scenes.cancel();
      message("Recall cancelled");
    }
  } else {
    return;
  }
  _selectedRow = _selectedCol = -1;
  markDirty();
}

void ScenePage::editorTouch(int x, int y)
{
  Scene &scene = _scenes.scene(_editScene);
  if (y >= kEditTopY && y < kEditTopY + kChipH) {
    if (x < 128) {
      scene.name = (scene.name + 1) % Scenes::kSceneNameCount;
    } else if (x < 240) {
      scene.timing = (SceneTiming)(((int)scene.timing + 1) % 3);
    } else {
      scene.tempo = 0; // tempo is set by Capture; tapping clears it
    }
    _scenes.changed(_editScene);
  } else if (y >= kEditRowsY && y < kEditRowsY + kEditRows * kEditRowH) {
    // Rows list the devices in use, in order
    int row = (y - kEditRowsY) / kEditRowH, device = -1;
    for (int d = 0, shown = 0; d < Scenes::kDevices; d++) {
      if (!_scenes.device(d).channel) continue;
      if (shown++ == row) {
        device = d;
        break;
      }
    }
    if (device < 0) return;
    int col = x >= kCcX ? 1 : (x >= kPcX ? 0 : -1);
    if (col == 1 && _selectedRow == device && _selectedCol == 1) {
      scene.devices[device].ccCount = 0; // tap the selected CC cell again: clear its CCs
      _scenes.changed(_editScene);
      message("CCs cleared");
    }
    _selectedRow = device;
    _selectedCol = col;
  } else if (y >= kEditChipsY && y < kEditChipsY + kChipH) {
    switch (x / 64) {
      case 0:
        if (!_scenes.capture(_editScene)) message("No snapshot slot free");
        else message("Captured");
        break;
      case 1: _scenes.clearCapture(_editScene); break;
      case 2: _scenes.setLearn(_scenes.learning() == _editScene ? -1 : _editScene); break;
      case 3: {
        int index = _scenes.addScene(_editScene);
        if (index >= 0) _editScene = index;
        else message("Setlist full (32)");
        break;
      }
      default:
        _scenes.removeScene(_editScene);
        if (_editScene >= _scenes.count()) _editScene = _scenes.count() - 1;
        break;
    }
  } else {
    return;
  }
  markDirty();
}

void ScenePage::devicesTouch(int x, int y)
{
  if (y < kDevRowsY || y >= kDevRowsY + Scenes::kDevices * kDevRowH) return;
  int d = (y - kDevRowsY) / kDevRowH;
  SceneDevice &device = _scenes.device(d);
  if (x < kDevNameW) {
    device.name = (device.name + 1) % Scenes::kDeviceNameCount;
  } else if (x < kDevOutX + kDevOutW) {
    device.output = device.output ? 0 : 1;
  } else if (x < kDevChX + kDevChW) {
    _selectedRow = d;
    _selectedCol = 2;
    markDirty();
    return;
  } else if (x < kDevPcX + kDevPcW) {
    device.pcAllowed = !device.pcAllowed;
    if (device.pcAllowed) message("PCs allowed: may switch patterns!");
  } else if (x < kDevLeadX + kDevLeadW) {
    _selectedRow = d;
    _selectedCol = 4;
    markDirty();
    return;
  } else {
    device.pcOneBased = !device.pcOneBased;
  }
  _scenes.changed(-1);
  markDirty();
}

void ScenePage::tick()
{
  SceneStatus status = _scenes.status();
  if (status.fired != _lastFired) {
    _lastFired = status.fired;
    _flashUntil = millis() + 200;
    markDirty();
  }
  renderLeds(status);
  if (_view != View::Live || millis() - _lastDraw < kRedrawMillis) return;
  int beats = status.pending && status.ticksLeft >= 0 ? (status.ticksLeft + 23) / 24 : -1;
  if (beats != _shownBeats || status.pending != _shown.pending || status.scene != _shown.scene) {
    markDirty();
  }
}

void ScenePage::renderLeds(const SceneStatus &status)
{
  if (!_visible) return;
  bool flash = (int32_t)(millis() - _flashUntil) < 0;
  int beats = status.pending && status.ticksLeft >= 0 ? (status.ticksLeft + 23) / 24 : 0;
  for (int i = 0; i < Leds::kCount; i++) {
    RGBColor color(0, 0, 0);
    if (flash) color = RGBColor(255, 255, 255); // a recall happened
    else if (status.pending && i < beats) color = RGBColor(255, 128, 0); // beats to the boundary
    else if (status.pending && beats == 0) color = RGBColor(255, 128, 0);
    _leds.setPixel(kLedOwner, i, color);
  }
}

void ScenePage::draw()
{
  SceneStatus status = _scenes.status();
  _shown = status;
  _shownBeats = status.pending && status.ticksLeft >= 0 ? (status.ticksLeft + 23) / 24 : -1;
  _lastDraw = millis();
  M5Canvas &c = canvas();
  c.fillRect(0, kContentTop, c.width(), kContentHeight, TFT_BLACK);
  switch (_view) {
    case View::Live: drawLive(status); break;
    case View::Editor: drawEditor(); break;
    default: drawDevices(); break;
  }
}

void ScenePage::drawLive(const SceneStatus &status)
{
  M5Canvas &c = canvas();
  char text[40], label[24];

  c.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  c.setCursor(4, kNowY + 12);
  c.print("Now");
  sceneLabel(_scenes.current(), label, sizeof(label));
  c.setTextSize(2);
  c.setTextColor(_scenes.current() >= 0 ? TFT_WHITE : TFT_DARKGREY, TFT_BLACK);
  c.setCursor(44, kNowY + 4);
  c.print(label);
  c.setTextSize(1);

  int armed = _scenes.armed();
  sceneLabel(armed, label, sizeof(label));
  const Scene &next = _scenes.scene(armed);
  snprintf(text, sizeof(text), "Next  %-18s%s", label, kTimingNames[(int)next.timing]);
  c.setTextColor(TFT_CYAN, TFT_BLACK);
  c.setCursor(4, kNextY);
  c.print(text);

  if (status.pending) {
    // Countdown to the boundary: a bar of beats
    int beats = status.ticksLeft >= 0 ? (status.ticksLeft + 23) / 24 : -1;
    sceneLabel(status.scene, label, sizeof(label));
    if (beats >= 0) snprintf(text, sizeof(text), "%s in %d beat%s", label, beats, beats == 1 ? "" : "s");
    else snprintf(text, sizeof(text), "%s queued", label);
    c.setTextColor(TFT_ORANGE, TFT_BLACK);
    c.setCursor(4, kCountdownY);
    c.print(text);
    int width = beats >= 0 ? (beats > 16 ? 16 : beats) * 19 : 304;
    c.fillRect(4, kCountdownY + 18, width, 6, TFT_ORANGE);
  } else if (status.pacedLeft) {
    c.setTextColor(TFT_DARKGREY, TFT_BLACK);
    c.setCursor(4, kCountdownY);
    c.print("sending CCs ...");
  }

  // The scenes after the armed one
  int x = 4;
  c.setTextColor(TFT_DARKGREY, TFT_BLACK);
  for (int i = armed + 1; i < _scenes.count() && i <= armed + 3; i++) {
    sceneLabel(i, label, sizeof(label));
    c.setCursor(x, kFollowingY);
    c.print(label);
    x += 104;
  }
  snprintf(text, sizeof(text), "%d scenes", _scenes.count());
  c.setCursor(4, kFollowingY + 24);
  c.print(text);

  drawChip(c, 0, kLiveChipsY, 107, kChipH, "Devices", true);
  drawChip(c, 107, kLiveChipsY, 107, kChipH, "New scene", true);
  drawChip(c, 214, kLiveChipsY, 106, kChipH, "Cancel", status.pending);
}

void ScenePage::drawEditor()
{
  M5Canvas &c = canvas();
  Scene &scene = _scenes.scene(_editScene);
  char text[48], label[24];

  sceneLabel(_editScene, label, sizeof(label));
  drawChip(c, 0, kEditTopY, 128, kChipH, label, true);
  drawChip(c, 128, kEditTopY, 112, kChipH, kTimingNames[(int)scene.timing], scene.timing != SceneTiming::Now);
  if (scene.tempo > 0) snprintf(text, sizeof(text), "%.1f", scene.tempo);
  else strcpy(text, "tempo --");
  drawChip(c, 240, kEditTopY, 80, kChipH, text, scene.tempo > 0);

  c.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  c.setCursor(4, kEditRowsY - 18);
  c.print("Device        PC (! = switches)  CCs");
  int shown = 0;
  for (int d = 0; d < Scenes::kDevices && shown < kEditRows; d++) {
    const SceneDevice &device = _scenes.device(d);
    if (!device.channel) continue;
    int y = kEditRowsY + shown++ * kEditRowH;
    const SceneDeviceMessages &m = scene.devices[d];
    snprintf(text, sizeof(text), "%-9s c%-2u", Scenes::kDeviceNames[device.name % Scenes::kDeviceNameCount],
             device.channel);
    c.setTextColor(TFT_WHITE, TFT_BLACK);
    c.setCursor(4, y + 1);
    c.print(text);
    bool pcSelected = _selectedRow == d && _selectedCol == 0;
    bool ccSelected = _selectedRow == d && _selectedCol == 1;
    if (m.program >= 0) {
      snprintf(text, sizeof(text), "PC %d%s", m.program + (device.pcOneBased ? 1 : 0), device.pcAllowed ? " !" : "");
    } else {
      strcpy(text, device.pcAllowed ? "PC --" : "PC off");
    }
    if (pcSelected) c.fillRect(kPcX, y, kPcW, kEditRowH, TFT_WHITE);
    c.setTextColor(pcSelected ? TFT_BLACK : (device.pcAllowed ? TFT_ORANGE : TFT_DARKGREY),
                   pcSelected ? TFT_WHITE : TFT_BLACK);
    c.setCursor(kPcX + 2, y + 1);
    c.print(text);
    snprintf(text, sizeof(text), "CC %u", m.ccCount);
    if (ccSelected) c.fillRect(kCcX, y, kCcW, kEditRowH, TFT_WHITE);
    c.setTextColor(ccSelected ? TFT_BLACK : TFT_WHITE, ccSelected ? TFT_WHITE : TFT_BLACK);
    c.setCursor(kCcX + 2, y + 1);
    c.print(ccSelected ? "CC tap: clear" : text);
  }
  if (!shown) {
    c.setTextColor(TFT_DARKGREY, TFT_BLACK);
    c.setCursor(4, kEditRowsY);
    c.print("No devices yet: Live view > Devices");
  }

  int n = 0;
  n += snprintf(text + n, sizeof(text) - n, "%s", scene.snapshot >= 0 ? "Router+Synth " : "");
  if (scene.drumPattern >= 0) n += snprintf(text + n, sizeof(text) - n, "Drums %d ", scene.drumPattern + 1);
  if (scene.fxPreset >= 0) n += snprintf(text + n, sizeof(text) - n, "FX %d ", scene.fxPreset + 1);
  if (scene.genSet) n += snprintf(text + n, sizeof(text) - n, "Gen ");
  if (!n) strcpy(text, "OxiMate: nothing captured");
  c.setTextColor(TFT_CYAN, TFT_BLACK);
  c.setCursor(4, kEditSummaryY);
  c.print(text);

  bool learning = _scenes.learning() == _editScene;
  drawChip(c, 0, kEditChipsY, 64, kChipH, "Capture", true);
  drawChip(c, 64, kEditChipsY, 64, kChipH, "Clear", true);
  drawChip(c, 128, kEditChipsY, 64, kChipH, learning ? "Learn*" : "Learn", learning);
  drawChip(c, 192, kEditChipsY, 64, kChipH, "New", true);
  drawChip(c, 256, kEditChipsY, 64, kChipH, "Delete", true);
}

void ScenePage::drawDevices()
{
  M5Canvas &c = canvas();
  char text[16];
  for (int d = 0; d < Scenes::kDevices; d++) {
    const SceneDevice &device = _scenes.device(d);
    int y = kDevRowsY + d * kDevRowH;
    bool used = device.channel != 0;
    drawChip(c, 0, y, kDevNameW, kDevRowH, Scenes::kDeviceNames[device.name % Scenes::kDeviceNameCount], used);
    drawChip(c, kDevOutX, y, kDevOutW, kDevRowH, device.output ? "USB" : "DIN", used);
    if (used) snprintf(text, sizeof(text), "ch%u", device.channel);
    else strcpy(text, "off");
    bool chSelected = _selectedRow == d && _selectedCol == 2;
    drawChip(c, kDevChX, y, kDevChW, kDevRowH, text, used || chSelected, chSelected ? TFT_WHITE : kOnColor);
    // Program changes switch Elektron patterns: off unless allowed per device
    drawChip(c, kDevPcX, y, kDevPcW, kDevRowH, device.pcAllowed ? "PC on!" : "PC off", device.pcAllowed,
             TFT_ORANGE);
    snprintf(text, sizeof(text), "L%u", device.leadBeats);
    bool leadSelected = _selectedRow == d && _selectedCol == 4;
    drawChip(c, kDevLeadX, y, kDevLeadW, kDevRowH, text, device.leadBeats || leadSelected,
             leadSelected ? TFT_WHITE : kOnColor);
    drawChip(c, kDevNumX, y, kDevNumW, kDevRowH, device.pcOneBased ? "1" : "0", false);
  }
}
