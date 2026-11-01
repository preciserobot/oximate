#include "clock_page.h"

#include <math.h>

using namespace Layout;

// Main view rows
static constexpr int kDigitsY = 28;
static constexpr int kSourceY = 80;
static constexpr int kPositionY = 110;
static constexpr int kBpmY = 132;
static constexpr int kSwingY = 162;
static constexpr int kExtraY = 192;
static constexpr int kChipHeight = 26;
static constexpr int kThird = 320 / 3;
static constexpr int kQuarter = 320 / 4;

// Outputs view rows
static constexpr int kOutputsHeaderY = 28;
static constexpr int kOutputRowY[2] = {50, 82}; // DIN, USB
static constexpr int kBeatsY = 124;
static constexpr int kClockChipX = 88, kTransportChipX = 160, kRatioChipX = 240;
static constexpr int kClockChipW = 70, kTransportChipW = 78, kRatioChipW = 76;

static constexpr uint32_t kDynamicMillis = 50; // redraw rate of the digits and position
static constexpr float kImuTapG = 1.5f;        // spike above the baseline that counts as a tap
static constexpr uint32_t kImuDeadMicros = 150000;
static constexpr uint32_t kImuPollMicros = 5000;

static const char *const kModeNames[] = {"INT", "USB", "DIN"};
static const int8_t kRatios[] = {-4, -3, -2, 1, 2, 3, 4};

static void ratioText(int8_t ratio, char *out, size_t size)
{
  snprintf(out, size, ratio < 0 ? "/%d" : "x%d", ratio < 0 ? -ratio : ratio);
}

static float roundBpm(float bpm)
{
  bpm = roundf(bpm * 10) / 10;
  return bpm < ClockEngine::kMinBpm ? ClockEngine::kMinBpm : (bpm > ClockEngine::kMaxBpm ? ClockEngine::kMaxBpm : bpm);
}

void ClockPage::buttonLabels(const char *labels[3]) const
{
  labels[0] = _clock.snapshot().running ? "Stop" : "Start";
  labels[1] = "Tap";
  labels[2] = _outputsView ? "Main" : "Outputs";
}

void ClockPage::onButton(int index)
{
  switch (index) {
    case 0:
      if (_clock.snapshot().running) {
        _clock.stop();
      } else {
        _clock.start();
      }
      break;
    case 1:
      tapNow(micros());
      break;
    case 2:
      _outputsView = !_outputsView;
      _partial = false;
      markDirty();
      break;
  }
}

void ClockPage::onButtonHold(int index)
{
  if (index == 0 && !_clock.snapshot().running) {
    _clock.cont(); // long press while stopped: continue from the current position
  }
}

void ClockPage::tapNow(uint32_t micros)
{
  float bpm = _tap.tap(micros);
  ClockConfig &config = _clock.config();
  if (bpm > 0 && config.mode == ClockMode::Internal) {
    config.bpm = roundBpm(bpm);
    changed();
  }
}

void ClockPage::changed()
{
  _clock.changed();
  _partial = false; // settings changed: chips need a full redraw
  markDirty();
}

void ClockPage::onTouch(int x, int y)
{
  ClockConfig &c = _clock.config();
  auto inRow = [y](int top) { return y >= top && y < top + kChipHeight; };

  if (_outputsView) {
    for (int o = 0; o < 2; o++) {
      if (!inRow(kOutputRowY[o])) continue;
      ClockOutputConfig &output = c.outputs[o];
      if (x >= kClockChipX && x < kClockChipX + kClockChipW) {
        output.enabled = !output.enabled;
      } else if (x >= kTransportChipX && x < kTransportChipX + kTransportChipW) {
        output.transport = !output.transport;
      } else if (x >= kRatioChipX) {
        int i = 0;
        while (i < 7 && kRatios[i] != output.ratio) i++;
        output.ratio = kRatios[(i + 1) % 7];
      } else {
        return;
      }
      changed();
      return;
    }
    if (inRow(kBeatsY) && x >= 160 && x < 240) {
      c.beatsPerBar = c.beatsPerBar >= 7 ? 2 : c.beatsPerBar + 1;
      changed();
    }
    return;
  }

  bool internal = c.mode == ClockMode::Internal;
  if (inRow(kSourceY)) {
    c.mode = (ClockMode)(x / kThird < 3 ? x / kThird : 2);
  } else if (inRow(kBpmY) && internal) {
    static const float kSteps[] = {-1.0f, -0.1f, 0.1f, 1.0f};
    c.bpm = roundBpm(c.bpm + kSteps[x / kQuarter < 4 ? x / kQuarter : 3]);
  } else if (inRow(kSwingY)) {
    switch (x / kQuarter) {
      case 0: if (c.swing > 50) c.swing--; break;
      case 1: c.swing = 50; break; // tap the value to straighten
      case 2: if (c.swing < 75) c.swing++; break;
      default: c.swingGrid = c.swingGrid == 16 ? 8 : 16; break;
    }
  } else if (inRow(kExtraY)) {
    if (x < 160) {
      c.imuTap = !c.imuTap;
    } else {
      _clock.locate(0); // rewind (only while stopped)
      return;
    }
  } else {
    return;
  }
  changed();
}

void ClockPage::enter()
{
  Page::enter();
  _partial = false;
  _visible = true;
  _leds.claim(kLedOwner, 0, Leds::kCount);
}

void ClockPage::leave()
{
  _visible = false;
  _leds.release(kLedOwner);
}

void ClockPage::draw()
{
  _shown = _clock.snapshot();
  DisplayLock lock;
  if (_partial && !_outputsView) {
    _partial = false;
    drawDynamic(); // digits and position only: no flicker on every beat
    return;
  }
  _partial = false;
  M5.Display.fillRect(0, kContentTop, M5.Display.width(), kContentHeight, TFT_BLACK);
  if (_outputsView) {
    drawOutputs();
  } else {
    drawMain();
  }
}

void ClockPage::drawDynamic()
{
  const ClockConfig &c = _clock.config();
  const ClockEngine::Snapshot &snap = _shown;
  bool internal = c.mode == ClockMode::Internal;

  // Big tempo digits (7-segment font), grey while waiting, red when the clock is lost
  char digits[8];
  uint16_t color = TFT_WHITE;
  if (!internal && (snap.lost || snap.bpm <= 0)) {
    strcpy(digits, "--.-");
    color = snap.lost ? TFT_RED : TFT_DARKGREY;
  } else {
    snprintf(digits, sizeof(digits), "%.1f", snap.bpm);
    if (!internal) color = snap.locked ? TFT_CYAN : TFT_YELLOW;
  }
  M5.Display.fillRect(0, kDigitsY, 320, 50, TFT_BLACK);
  M5.Display.setFont(&fonts::Font7);
  M5.Display.setTextColor(color);
  M5.Display.setTextDatum(top_right);
  M5.Display.drawString(digits, 240, kDigitsY);
  M5.Display.setFont(&fonts::AsciiFont8x16);
  M5.Display.setTextDatum(top_left);
  M5.Display.drawString(snap.lost && !internal ? "LOST" : "BPM", 250, kDigitsY + 30);

  // Position and beat dots
  M5.Display.fillRect(0, kPositionY, 320, 18, TFT_BLACK);
  char position[32];
  snprintf(position, sizeof(position), "Bar %04u  Beat %u/%u", snap.bar + 1, snap.beat + 1, snap.beatsPerBar);
  M5.Display.setTextColor(snap.running ? TFT_WHITE : TFT_DARKGREY);
  M5.Display.drawString(position, 4, kPositionY);
  for (int b = 0; b < snap.beatsPerBar; b++) {
    int cx = 210 + b * 15;
    if (b == snap.beat && snap.running) {
      M5.Display.fillCircle(cx, kPositionY + 7, 5, b == 0 ? TFT_ORANGE : TFT_WHITE);
    } else {
      M5.Display.drawCircle(cx, kPositionY + 7, 5, TFT_DARKGREY);
    }
  }
}

void ClockPage::drawMain()
{
  const ClockConfig &c = _clock.config();
  const ClockEngine::Snapshot &snap = _shown;
  bool internal = c.mode == ClockMode::Internal;

  drawDynamic();
  for (int m = 0; m < 3; m++) {
    drawChip(m * kThird, kSourceY, kThird, kChipHeight, kModeNames[m], (int)c.mode == m);
  }

  static const char *const kBpmSteps[] = {"-1", "-0.1", "+0.1", "+1"};
  for (int i = 0; i < 4; i++) {
    drawChip(i * kQuarter, kBpmY, kQuarter, kChipHeight, kBpmSteps[i], internal);
  }

  char swing[12], grid[12];
  snprintf(swing, sizeof(swing), "Sw %u%%", c.swing);
  snprintf(grid, sizeof(grid), "Grid 1/%u", c.swingGrid);
  drawChip(0 * kQuarter, kSwingY, kQuarter, kChipHeight, "Swing-", true);
  drawChip(1 * kQuarter, kSwingY, kQuarter, kChipHeight, swing, c.swing > 50);
  drawChip(2 * kQuarter, kSwingY, kQuarter, kChipHeight, "Swing+", true);
  drawChip(3 * kQuarter, kSwingY, kQuarter, kChipHeight, grid, true);

  drawChip(0, kExtraY, 160, kChipHeight, c.imuTap ? "IMU tap on" : "IMU tap off", c.imuTap);
  drawChip(160, kExtraY, 160, kChipHeight, "Rewind", !snap.running);
}

void ClockPage::drawOutputs()
{
  const ClockConfig &c = _clock.config();
  M5.Display.setTextColor(TFT_LIGHTGREY);
  M5.Display.drawString("Output   Clock   Transport  Ratio", 4, kOutputsHeaderY);
  static const char *const kNames[] = {"DIN OUT", "USB"};
  for (int o = 0; o < 2; o++) {
    const ClockOutputConfig &output = c.outputs[o];
    int y = kOutputRowY[o];
    M5.Display.setTextColor(o == 0 ? kDinColor : kUsbColor);
    M5.Display.drawString(kNames[o], 4, y + 5);
    char ratio[6];
    ratioText(output.ratio, ratio, sizeof(ratio));
    drawChip(kClockChipX, y, kClockChipW, kChipHeight, output.enabled ? "on" : "off", output.enabled);
    drawChip(kTransportChipX, y, kTransportChipW, kChipHeight, output.transport ? "on" : "off", output.transport);
    drawChip(kRatioChipX, y, kRatioChipW, kChipHeight, ratio, output.ratio != 1);
  }
  char beats[4];
  snprintf(beats, sizeof(beats), "%u", c.beatsPerBar);
  M5.Display.setTextColor(TFT_WHITE);
  M5.Display.drawString("Beats per bar", 4, kBeatsY + 5);
  drawChip(160, kBeatsY, 80, kChipHeight, beats, true);
}

void ClockPage::tick()
{
  ClockEngine::Snapshot snap = _clock.snapshot();
  renderLeds(snap);
  // The Start/Stop label follows the real transport state: commands take effect in the MIDI
  // task a moment after the button press, and a followed clock starts and stops on its own
  if (snap.running != _labelRunning) {
    _labelRunning = snap.running;
    markFooterDirty();
  }

  static uint32_t lastDraw = 0;
  if (_outputsView || millis() - lastDraw < kDynamicMillis) {
    return;
  }
  // Redraw when anything visible changed (tempo to 0.1, position, transport, sync state)
  bool changedView = roundf(snap.bpm * 10) != roundf(_shown.bpm * 10) || snap.beat != _shown.beat ||
                     snap.bar != _shown.bar || snap.running != _shown.running || snap.lost != _shown.lost ||
                     snap.locked != _shown.locked || snap.mode != _shown.mode;
  if (changedView) {
    lastDraw = millis();
    // Transport and source changes alter chips too; everything else only the dynamic parts
    bool structural = snap.running != _shown.running || snap.mode != _shown.mode;
    if (!dirty()) _partial = !structural;
    markDirty();
  }
}

void ClockPage::renderLeds(const ClockEngine::Snapshot &snap)
{
  if (!_visible) return;
  if (snap.bar != _lastBar) {
    _lastBar = snap.bar;
    _lastBarMillis = millis();
  }
  const RGBColor off(0, 0, 0);
  for (int i = 0; i < 4; i++) { // beat within the bar
    RGBColor color = off;
    if (snap.running && i == snap.beat % 4) color = snap.beat == 0 ? RGBColor(255, 128, 0) : RGBColor(255, 255, 255);
    _leds.setPixel(kLedOwner, i, color);
  }
  bool downbeat = snap.running && snap.beat == 0 && millis() - _lastBarMillis < 150;
  _leds.setPixel(kLedOwner, 4, downbeat ? RGBColor(255, 128, 0) : off);
  for (int i = 0; i < 4; i++) { // bar within a 4-bar phrase
    _leds.setPixel(kLedOwner, 5 + i, snap.running && i == snap.bar % 4 ? RGBColor(0, 64, 255) : off);
  }
  RGBColor sync = RGBColor(0, 255, 0); // internal
  if (snap.mode != ClockMode::Internal) {
    sync = snap.lost ? RGBColor(255, 0, 0) : (snap.locked ? RGBColor(0, 0, 255) : RGBColor(255, 200, 0));
  }
  _leds.setPixel(kLedOwner, 9, sync);
}

void ClockPage::pollImu()
{
  if (!_clock.config().imuTap) return;
  uint32_t now = micros();
  if (now - _lastImuMicros < kImuPollMicros) return;
  _lastImuMicros = now;
  float ax, ay, az;
  if (!M5.Imu.getAccel(&ax, &ay, &az)) return;
  float magnitude = sqrtf(ax * ax + ay * ay + az * az);
  _baseline += (magnitude - _baseline) * 0.02f;
  if (magnitude - _baseline > kImuTapG && now - _lastTapMicros > kImuDeadMicros) {
    _lastTapMicros = now;
    tapNow(now);
  }
}
