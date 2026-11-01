#include "synth_page.h"

#include "gm_names.h"

using namespace Layout;

// Main view geometry
static constexpr int kTopY = 24, kTopH = 22;
static constexpr int kTableHeaderY = 48;
static constexpr int kLayerY = 64, kLayerH = 20;
static constexpr int kGlobalY = 166, kGlobalH = 22;
static constexpr int kWiringY = 192, kWiringH = 22;
static constexpr int kLayerColX[5] = {0, 24, 80, 256, 288}; // label, channel, instrument, volume, reverb
static constexpr int kLayerColW[4] = {56, 176, 32, 32};
static constexpr int kTopColX[3] = {0, 120, 200}, kTopColW[3] = {120, 80, 120};

// Browser geometry
static constexpr int kFamilyY = 24, kFamilyH = 22, kFamilyW = 40;
static constexpr int kListY = 72, kListH = 18;

static constexpr uint32_t kRepeatDelayMillis = 400, kRepeatMillis = 60;

static const char *const kListenNames[] = {"All", "Layers", "Off"};
static const char *const kVoicesNames[] = {"Full 48", "Lite 61", "Dry 64"};
static const char *const kReverbNames[] = {"Room1", "Room2", "Room3", "Hall1", "Hall2", "Plate", "Delay", "PanDly"};
static const char *const kLayerLabels[] = {"1", "2", "3", "4", "D"};

static int clampValue(int value, int low, int high) { return value < low ? low : (value > high ? high : value); }

void SynthPage::instrumentText(const SynthLayer &layer, bool drums, char *out, size_t size) const
{
  if (drums) {
    snprintf(out, size, "Kit %s", GmNames::kitName(layer.program));
  } else if (layer.bank == 127) {
    snprintf(out, size, "MT %03u", layer.program + 1); // MT-32 bank: names not in the datasheet text
  } else {
    snprintf(out, size, "%03u %s", layer.program + 1, GmNames::kPrograms[layer.program]);
  }
}

void SynthPage::buttonLabels(const char *labels[3]) const
{
  if (_browsing) {
    labels[0] = "Back";
    labels[1] = _browseLayer == SamSynth::kDrumLayer ? "" : (_browseBank == 127 ? "Bank:MT" : "Bank:GM");
    labels[2] = "Select";
    return;
  }
  labels[0] = "-";
  labels[1] = instrumentSelected() ? "Browse" : (selectedLayer() >= 0 ? "Test" : "");
  labels[2] = "+";
}

void SynthPage::enter()
{
  Page::enter();
  _browsing = false;
}

void SynthPage::changed()
{
  _synth.changed();
  markDirty();
}

void SynthPage::openBrowser(int layer)
{
  const SynthLayer &l = _synth.config().layers[layer];
  _browsing = true;
  _browseLayer = layer;
  _browseBank = l.bank;
  _browseProgram = l.program;
  _browseFamily = l.program / 8;
  markDirty();
}

void SynthPage::onButton(int index)
{
  SynthConfig &c = _synth.config();
  if (_browsing) {
    switch (index) {
      case 0: // back without changing the layer
        _browsing = false;
        markDirty();
        break;
      case 1:
        if (_browseLayer != SamSynth::kDrumLayer) {
          _browseBank = _browseBank == 127 ? 0 : 127;
          markDirty();
        }
        break;
      case 2: // select: the browsed sound becomes the layer's sound
        c.layers[_browseLayer].bank = _browseLayer == SamSynth::kDrumLayer ? 0 : _browseBank;
        c.layers[_browseLayer].program = _browseProgram;
        _browsing = false;
        changed();
        break;
    }
    return;
  }
  switch (index) {
    case 0: adjust(-1); break;
    case 2: adjust(+1); break;
    case 1:
      if (instrumentSelected()) {
        openBrowser(selectedLayer());
      } else if (selectedLayer() >= 0) {
        _synth.testLayer(selectedLayer());
      }
      break;
  }
}

void SynthPage::tick()
{
  // Holding A or C repeats - / +
  if (_browsing) return;
  m5::Button_Class *buttons[2] = {&M5.BtnA, &M5.BtnC};
  for (int i = 0; i < 2; i++) {
    if (buttons[i]->pressedFor(kRepeatDelayMillis) && millis() - _repeatAt >= kRepeatMillis) {
      _repeatAt = millis();
      adjust(i == 0 ? -1 : +1);
    }
  }
}

void SynthPage::adjust(int direction)
{
  SynthConfig &c = _synth.config();
  int row = _selected.row, col = _selected.col;
  if (row == 0) {
    if (col == 1) c.configChannel = clampValue(c.configChannel + direction, 1, 16);
    else if (col == 0) c.listen = (SynthListen)(((int)c.listen + 3 + direction) % 3);
    else c.voices = (SynthVoices)(((int)c.voices + 3 + direction) % 3);
  } else if (row >= 1 && row <= 5) {
    int layer = row - 1;
    SynthLayer &l = c.layers[layer];
    bool drums = layer == SamSynth::kDrumLayer;
    switch (col) {
      case 0: { // listen channel: off, 1-16, never the config channel
        int channel = l.channel;
        do {
          channel = (channel + 17 + direction) % 17;
        } while (channel == c.configChannel && channel != 0);
        l.channel = channel;
        break;
      }
      case 1:
        if (drums) {
          int k = 0;
          while (k < GmNames::kKitCount && GmNames::kKits[k].program != l.program) k++;
          k = (k + GmNames::kKitCount + direction) % GmNames::kKitCount;
          l.program = GmNames::kKits[k].program;
        } else {
          l.program = (l.program + 128 + direction) % 128;
        }
        break;
      case 2: l.volume = clampValue(l.volume + direction, 0, 127); break;
      default: l.reverb = clampValue(l.reverb + direction, 0, 127); break;
    }
  } else if (row == 6) {
    switch (col) {
      case 0: c.masterVolume = clampValue(c.masterVolume + direction, 0, 127); break;
      case 1: c.reverbType = (c.reverbType + 8 + direction) % 8; break;
      case 2: c.reverbLevel = clampValue(c.reverbLevel + direction, 0, 127); break;
      default: c.chorusLevel = clampValue(c.chorusLevel + direction, 0, 127); break;
    }
  } else {
    return; // wiring and resend act on tap
  }
  changed();
}

void SynthPage::onTouch(int x, int y)
{
  if (_browsing) {
    browserTouch(x, y);
    return;
  }
  SynthConfig &c = _synth.config();
  Cell cell = {-1, -1};
  if (y >= kTopY && y < kTopY + kTopH) {
    cell = {0, (int8_t)(x < kTopColX[1] ? 0 : (x < kTopColX[2] ? 1 : 2))};
  } else if (y >= kLayerY && y < kLayerY + 5 * kLayerH) {
    int8_t col = x < kLayerColX[2] ? 0 : (x < kLayerColX[3] ? 1 : (x < kLayerColX[4] ? 2 : 3));
    cell = {(int8_t)(1 + (y - kLayerY) / kLayerH), col};
  } else if (y >= kGlobalY && y < kGlobalY + kGlobalH) {
    cell = {6, (int8_t)(x / 80)};
  } else if (y >= kWiringY && y < kWiringY + kWiringH && x < 200) {
    cell = {7, (int8_t)(x < 120 ? 0 : 1)};
  } else {
    return;
  }

  // Tapping an already selected cell, or a cell that is an action, acts on it
  bool again = cell.row == _selected.row && cell.col == _selected.col;
  _selected = cell;
  if (cell.row == 0 && cell.col != 1) { // mode and voices cycle on every tap
    adjust(+1);
  } else if (cell.row == 7 && cell.col == 0) {
    c.bypass = !c.bypass;
    changed();
  } else if (cell.row == 7 && cell.col == 1) {
    _synth.resend();
  } else if (again && instrumentSelected()) {
    openBrowser(selectedLayer());
  }
  markDirty();
}

void SynthPage::browserTouch(int x, int y)
{
  bool drums = _browseLayer == SamSynth::kDrumLayer;
  if (drums) {
    int index = (y - kListY + 2 * kListH) / kListH; // kits start higher: no family grid
    if (y >= kListY - 2 * kListH && index >= 0 && index < GmNames::kKitCount) {
      _browseProgram = GmNames::kKits[index].program;
      _synth.preview(_browseLayer, 0, _browseProgram);
      markDirty();
    }
    return;
  }
  if (y >= kFamilyY && y < kFamilyY + 2 * kFamilyH) {
    _browseFamily = ((y - kFamilyY) / kFamilyH) * 8 + x / kFamilyW;
    markDirty();
  } else if (y >= kListY && y < kListY + 8 * kListH) {
    _browseProgram = _browseFamily * 8 + (y - kListY) / kListH;
    _synth.preview(_browseLayer, _browseBank, _browseProgram);
    markDirty();
  }
}

void SynthPage::draw()
{
  DisplayLock lock;
  M5.Display.fillRect(0, kContentTop, M5.Display.width(), kContentHeight, TFT_BLACK);
  if (_browsing) {
    drawBrowser();
  } else {
    drawMain();
  }
}

/*! A table cell: inverted when selected */
static void cell(int x, int y, int w, int h, const char *text, bool selected, uint16_t color = TFT_WHITE)
{
  if (selected) M5.Display.fillRect(x, y, w, h, color);
  M5.Display.setTextColor(selected ? TFT_BLACK : color, selected ? color : TFT_BLACK);
  M5.Display.setCursor(x + 2, y + (h - kCharHeight) / 2);
  M5.Display.print(text);
}

void SynthPage::drawMain()
{
  const SynthConfig &c = _synth.config();
  auto isSelected = [this](int row, int col) { return _selected.row == row && _selected.col == col; };
  char text[40];

  snprintf(text, sizeof(text), "Mode %s", kListenNames[(int)c.listen]);
  drawChip(kTopColX[0], kTopY, kTopColW[0], kTopH, text, c.listen == SynthListen::Layers);
  snprintf(text, sizeof(text), "Cfg %u", c.configChannel);
  drawChip(kTopColX[1], kTopY, kTopColW[1], kTopH, text, isSelected(0, 1));
  drawChip(kTopColX[2], kTopY, kTopColW[2], kTopH, kVoicesNames[(int)c.voices], c.voices != SynthVoices::Dry);

  bool layers = c.listen == SynthListen::Layers;
  M5.Display.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  M5.Display.setCursor(0, kTableHeaderY);
  M5.Display.print(layers ? " # Chan Instrument          Vol Rev" : " Layers apply in Mode Layers");

  for (int layer = 0; layer < SamSynth::kLayers; layer++) {
    const SynthLayer &l = c.layers[layer];
    int y = kLayerY + layer * kLayerH;
    int row = layer + 1;
    uint16_t color = layers && l.channel ? TFT_WHITE : TFT_DARKGREY;
    cell(kLayerColX[0], y, 24, kLayerH, kLayerLabels[layer], false, TFT_LIGHTGREY);
    if (l.channel) snprintf(text, sizeof(text), "ch%u", l.channel);
    else strcpy(text, "off");
    cell(kLayerColX[1], y, kLayerColW[0], kLayerH, text, isSelected(row, 0), color);
    instrumentText(l, layer == SamSynth::kDrumLayer, text, sizeof(text));
    text[21] = '\0'; // fits the column
    cell(kLayerColX[2], y, kLayerColW[1], kLayerH, text, isSelected(row, 1), color);
    snprintf(text, sizeof(text), "%3u", l.volume);
    cell(kLayerColX[3], y, kLayerColW[2], kLayerH, text, isSelected(row, 2), color);
    snprintf(text, sizeof(text), "%3u", l.reverb);
    cell(kLayerColX[4], y, kLayerColW[3], kLayerH, text, isSelected(row, 3), color);
  }

  const char *globals[4];
  char master[12], reverb[12], level[12], chorus[12];
  snprintf(master, sizeof(master), "Mst %u", c.masterVolume);
  snprintf(reverb, sizeof(reverb), "%s", kReverbNames[c.reverbType & 7]);
  snprintf(level, sizeof(level), "Rev %u", c.reverbLevel);
  snprintf(chorus, sizeof(chorus), "Cho %u", c.chorusLevel);
  globals[0] = master;
  globals[1] = reverb;
  globals[2] = level;
  globals[3] = chorus;
  for (int i = 0; i < 4; i++) {
    drawChip(i * 80, kGlobalY, 80, kGlobalH, globals[i], isSelected(6, i));
  }

  drawChip(0, kWiringY, 120, kWiringH, c.bypass ? "Wiring:Bypass" : "Wiring:Separ.", true);
  drawChip(120, kWiringY, 80, kWiringH, "Resend", true);
  if (!c.bypass) {
    // Decided 2026-11-01: warn that synth settings reach DIN gear (Elektrons switch patterns on PC)
    M5.Display.setFont(&fonts::Font0);
    M5.Display.setTextColor(TFT_ORANGE, TFT_BLACK);
    snprintf(text, sizeof(text), "PC/CC on ch%u", c.configChannel);
    M5.Display.drawString(text, 204, kWiringY + 2);
    M5.Display.drawString("reach DIN gear!", 204, kWiringY + 12);
    M5.Display.setFont(&fonts::AsciiFont8x16);
  }
}

void SynthPage::drawBrowser()
{
  char text[40];
  bool drums = _browseLayer == SamSynth::kDrumLayer;
  if (drums) {
    for (int k = 0; k < GmNames::kKitCount; k++) {
      bool current = GmNames::kKits[k].program == _browseProgram;
      snprintf(text, sizeof(text), "%c%03u %s", current ? '>' : ' ', GmNames::kKits[k].program + 1,
               GmNames::kKits[k].name);
      cell(0, kListY - 2 * kListH + k * kListH, 320, kListH, text, current);
    }
    return;
  }
  for (int f = 0; f < 16; f++) {
    drawChip((f % 8) * kFamilyW, kFamilyY + (f / 8) * kFamilyH, kFamilyW, kFamilyH, GmNames::kFamilies[f],
             f == _browseFamily);
  }
  for (int i = 0; i < 8; i++) {
    uint8_t program = _browseFamily * 8 + i;
    bool current = program == _browseProgram;
    if (_browseBank == 127) {
      snprintf(text, sizeof(text), "%cMT %03u (GM: %s)", current ? '>' : ' ', program + 1, GmNames::kPrograms[program]);
    } else {
      snprintf(text, sizeof(text), "%c%03u %s", current ? '>' : ' ', program + 1, GmNames::kPrograms[program]);
    }
    cell(0, kListY + i * kListH, 320, kListH, text, current);
  }
}
