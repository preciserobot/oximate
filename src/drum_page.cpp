#include "drum_page.h"

#include "drum_kits.h"

using namespace Layout;

static constexpr int kLaneY = 24, kLaneH = 22, kLaneW = 40;

// Grid view
static constexpr int kPadY = 48, kPadH = 42, kPadW = 40;
static constexpr int kOverviewY = 138, kOverviewRowH = 6, kOverviewStepW = 10;
static constexpr int kStatusY = 190, kStatusH = 22;

// Sound view
static constexpr int kRowY[4] = {52, 80, 108, 136};
static constexpr int kRowH = 24, kButtonW = 40;
static constexpr int kSoundChipsY = 170;

// Pattern view
static constexpr int kSlotX = 2, kSlotY = 26, kSlotW = 48, kSlotH = 34;
static constexpr int kSideX = 200;

static constexpr uint32_t kHitFlashMillis = 80;

/*! A short code for a lane's drum note */
static void laneLabel(uint8_t note, char *out, size_t size)
{
  const char *code = nullptr;
  switch (note) {
    case 35: case 36: code = "BD"; break;
    case 37: code = "RS"; break;
    case 38: case 40: code = "SD"; break;
    case 39: code = "CP"; break;
    case 42: code = "CH"; break;
    case 44: code = "PH"; break;
    case 46: code = "OH"; break;
    case 41: case 43: case 45: code = "LT"; break;
    case 47: case 48: code = "MT"; break;
    case 50: code = "HT"; break;
    case 49: case 52: case 55: case 57: code = "CY"; break;
    case 51: case 59: code = "RD"; break;
    case 53: code = "BL"; break;
    case 54: code = "TB"; break;
    case 56: code = "CB"; break;
  }
  if (code) snprintf(out, size, "%s", code);
  else snprintf(out, size, "%u", note);
}

/*! Lane colour: a hue per lane */
static uint16_t laneColor(int lane, bool bright)
{
  static const uint16_t kColors[8] = {TFT_RED, TFT_ORANGE, TFT_YELLOW, TFT_GREENYELLOW,
                                      TFT_CYAN, TFT_BLUE, TFT_VIOLET, TFT_MAGENTA};
  uint16_t c = kColors[lane & 7];
  if (bright) return c;
  // darker version for normal (non-accent) steps
  uint8_t r = (c >> 11) & 0x1F, g = (c >> 5) & 0x3F, b = c & 0x1F;
  return ((r * 3 / 5) << 11) | ((g * 3 / 5) << 5) | (b * 3 / 5);
}

bool DrumPage::following() const { return _clock.config().mode != ClockMode::Internal; }

void DrumPage::buttonLabels(const char *labels[3]) const
{
  bool running = _clock.snapshot().running;
  if (following()) {
    labels[0] = _drums.enabled() ? "Disarm" : "Arm";
  } else {
    labels[0] = running ? "Stop" : "Play";
  }
  const DrumLane &lane = _drums.bank().lanes[_lane];
  labels[1] = _view == Sound ? "Audit" : (lane.muted ? "Unmute" : "Mute");
  labels[2] = "View>";
}

void DrumPage::onButton(int index)
{
  switch (index) {
    case 0:
      if (following()) {
        _drums.setEnabled(!_drums.enabled()); // transport belongs to the external master
      } else if (_clock.snapshot().running) {
        _clock.stop();
      } else {
        _drums.setEnabled(true);
        _clock.start();
      }
      break;
    case 1:
      if (_view == Sound) {
        _drums.audition(_lane);
      } else {
        DrumLane lane = _drums.bank().lanes[_lane];
        lane.muted = !lane.muted;
        _drums.setLane(_lane, lane);
      }
      break;
    case 2:
      _view = (View)((_view + 1) % kViews);
      break;
  }
  markDirty();
}

void DrumPage::onButtonHold(int index)
{
  if (index == 1) { // hold B: solo the selected lane
    DrumLane lane = _drums.bank().lanes[_lane];
    lane.solo = !lane.solo;
    _drums.setLane(_lane, lane);
    markDirty();
  }
}

void DrumPage::enter()
{
  Page::enter();
  _leds.claim(kLedOwner, 0, Leds::kCount);
}

void DrumPage::leave() { _leds.release(kLedOwner); }

void DrumPage::tick()
{
  renderLeds();
  bool running = _clock.snapshot().running;
  if (running != _labelRunning) {
    _labelRunning = running;
    markFooterDirty();
    markDirty();
  }
  // The playhead moves on every step: redraw then (at most a few times per second per step rate)
  int position = _drums.playing() ? _drums.position() : -1;
  if (position != _shownPosition) {
    _shownPosition = position;
    markDirty();
  }
}

void DrumPage::renderLeds()
{
  uint32_t hits = _drums.hitCount();
  if (hits != _shownHits) {
    _shownHits = hits;
    _hitFlashMillis = millis();
  }
  int position = _drums.playing() ? _drums.position() : -1;
  const RGBColor off(0, 0, 0);
  // Right strip (LEDs 0-4): 16th within the beat, then the hit flash
  for (int i = 0; i < 4; i++) {
    _leds.setPixel(kLedOwner, i, position >= 0 && position % 4 == i ? RGBColor(255, 255, 255) : off);
  }
  bool flash = millis() - _hitFlashMillis < kHitFlashMillis;
  _leds.setPixel(kLedOwner, 4, flash ? RGBColor(255, 64, 0) : off);
  // Left strip (LEDs 5-9): beat within the bar (beat 1 brighter), then steps 17-32 marker
  for (int i = 0; i < 4; i++) {
    bool lit = position >= 0 && (position / 4) % 4 == i;
    _leds.setPixel(kLedOwner, 5 + i, lit ? (i == 0 ? RGBColor(255, 128, 0) : RGBColor(0, 96, 255)) : off);
  }
  _leds.setPixel(kLedOwner, 9, position >= 16 ? RGBColor(0, 255, 0) : off);
}

// ---------------------------------------------------------------------------- touch

void DrumPage::onTouch(int x, int y)
{
  if (y >= kLaneY && y < kLaneY + kLaneH && _view != Pattern) {
    int lane = x / kLaneW;
    if (lane >= 0 && lane < DrumMachine::kLanes) {
      _lane = lane;
      _drums.audition(lane); // hear what you selected
      markDirty();
      markFooterDirty();
    }
    return;
  }
  switch (_view) {
    case Grid: touchGrid(x, y); break;
    case Sound: touchSound(x, y); break;
    default: touchPattern(x, y); break;
  }
}

void DrumPage::touchGrid(int x, int y)
{
  int pattern = editPattern();
  if (y >= kPadY && y < kPadY + 2 * kPadH) {
    // Each tap cycles the step: off -> on -> accent -> off
    int step = _padPage * 16 + ((y - kPadY) / kPadH) * 8 + x / kPadW;
    if (step >= _drums.bank().patterns[pattern].length) return;
    DrumStep current = _drums.step(pattern, _lane, step);
    DrumStep next = current == DrumStep::Off ? DrumStep::On : (current == DrumStep::On ? DrumStep::Accent : DrumStep::Off);
    _drums.setStep(pattern, _lane, step, next);
  } else if (y >= kOverviewY && y < kOverviewY + DrumMachine::kLanes * kOverviewRowH + 2) {
    _lane = (y - kOverviewY) / kOverviewRowH;
    if (_lane >= DrumMachine::kLanes) _lane = DrumMachine::kLanes - 1;
    _padPage = x / (16 * kOverviewStepW) ? 1 : 0;
    markFooterDirty();
  } else if (y >= kStatusY && y < kStatusY + kStatusH) {
    if (x < 64) _padPage ^= 1;
    else if (x < 128) _drums.setRecording(!_drums.recording());
  } else {
    return;
  }
  markDirty();
}

void DrumPage::touchSound(int x, int y)
{
  int direction = x < kButtonW ? -1 : (x >= 320 - kButtonW ? +1 : 0);
  DrumLane lane = _drums.bank().lanes[_lane];
  auto clamp = [](int v, int low, int high) { return v < low ? low : (v > high ? high : v); };
  if (direction && y >= kRowY[0] && y < kRowY[0] + kRowH) {
    lane.note = clamp(lane.note + direction, 0, 127);
    _drums.setLane(_lane, lane);
    _drums.audition(_lane);
  } else if (direction && y >= kRowY[1] && y < kRowY[1] + kRowH) {
    lane.velocity = clamp(lane.velocity + direction * 5, 1, 127);
    _drums.setLane(_lane, lane);
  } else if (direction && y >= kRowY[2] && y < kRowY[2] + kRowH) {
    lane.accentVelocity = clamp(lane.accentVelocity + direction * 5, 1, 127);
    _drums.setLane(_lane, lane);
  } else if (direction && y >= kRowY[3] && y < kRowY[3] + kRowH) {
    _drums.setPadChannel((_drums.bank().padChannel + 17 + direction) % 17);
  } else if (y >= kSoundChipsY && y < kSoundChipsY + kStatusH && x < 160) {
    _drums.setUsbOut(!_drums.bank().usbOut);
  } else {
    return;
  }
  markDirty();
}

void DrumPage::touchPattern(int x, int y)
{
  if (x < kSideX) {
    int col = (x - kSlotX) / kSlotW, row = (y - kSlotY) / (kSlotH + 2);
    if (y < kSlotY || col < 0 || col > 3 || row < 0 || row > 3) return;
    int pattern = row * 4 + col;
    if (_chainEdit) {
      if (_chainLength < DrumMachine::kPatterns) _chain[_chainLength++] = pattern;
      _drums.setChain(_chain, _chainLength);
    } else {
      _drums.queuePattern(pattern);
    }
    markDirty();
    return;
  }
  int pattern = editPattern();
  if (y >= 46 && y < 72) { // length - / +
    int length = _drums.bank().patterns[pattern].length;
    _drums.setLength(pattern, x < kSideX + 40 ? length - 1 : (x >= 320 - 40 ? length + 1 : length));
  } else if (y >= 80 && y < 104) { // chain on / off (off clears it)
    _chainEdit = !_chainEdit;
    _chainLength = 0;
    _drums.setChain(_chain, 0);
  } else if (y >= 112 && y < 136) {
    _drums.clearPattern(pattern);
  } else {
    return;
  }
  markDirty();
}

// ---------------------------------------------------------------------------- drawing

void DrumPage::draw()
{
  M5Canvas &c = canvas();
  c.fillRect(0, kContentTop, c.width(), kContentHeight, TFT_BLACK);
  if (_view != Pattern) drawLanes();
  switch (_view) {
    case Grid: drawGrid(); break;
    case Sound: drawSound(); break;
    default: drawPattern(); break;
  }
}

void DrumPage::drawLanes()
{
  M5Canvas &c = canvas();
  char text[6];
  for (int lane = 0; lane < DrumMachine::kLanes; lane++) {
    const DrumLane &l = _drums.bank().lanes[lane];
    laneLabel(l.note, text, sizeof(text));
    uint16_t color = l.muted ? TFT_DARKGREY : (l.solo ? TFT_WHITE : laneColor(lane, true));
    drawChip(c, lane * kLaneW, kLaneY, kLaneW, kLaneH, text, lane == _lane, color);
    if (lane != _lane) { // unselected: coloured label on dark chip
      c.setTextColor(color);
      c.setTextDatum(middle_center);
      c.drawString(text, lane * kLaneW + kLaneW / 2, kLaneY + kLaneH / 2);
      c.setTextDatum(top_left);
    }
  }
}

void DrumPage::drawGrid()
{
  M5Canvas &c = canvas();
  int pattern = editPattern();
  const DrumPattern &p = _drums.bank().patterns[pattern];
  int position = _drums.playing() ? _drums.position() : -1;
  bool muted = _drums.bank().lanes[_lane].muted;

  for (int i = 0; i < 16; i++) {
    int step = _padPage * 16 + i;
    int x = (i % 8) * kPadW, y = kPadY + (i / 8) * kPadH;
    uint32_t bit = 1u << step;
    if (step >= p.length) {
      c.drawRoundRect(x + 2, y + 2, kPadW - 4, kPadH - 4, 4, 0x2945); // beyond the pattern length
      continue;
    }
    bool on = p.on[_lane] & bit, accent = p.accent[_lane] & bit;
    uint16_t color = !on ? 0x2945 : laneColor(_lane, accent);
    if (muted && on) color = TFT_DARKGREY;
    c.fillRoundRect(x + 2, y + 2, kPadW - 4, kPadH - 4, 4, color);
    if (accent) c.fillCircle(x + kPadW / 2, y + kPadH / 2, 4, TFT_WHITE);
    if (step == position) c.drawRoundRect(x + 1, y + 1, kPadW - 2, kPadH - 2, 5, TFT_WHITE);
    char number[4];
    snprintf(number, sizeof(number), "%d", step + 1);
    c.setFont(&fonts::Font0);
    c.setTextColor(on ? TFT_BLACK : TFT_LIGHTGREY);
    c.drawString(number, x + 5, y + 5);
    c.setFont(&fonts::AsciiFont8x16);
  }

  // Overview: all lanes, all steps
  for (int lane = 0; lane < DrumMachine::kLanes; lane++) {
    int y = kOverviewY + lane * kOverviewRowH;
    if (lane == _lane) c.fillRect(0, y, 320, kOverviewRowH, 0x18E3);
    for (int step = 0; step < p.length; step++) {
      uint32_t bit = 1u << step;
      if (p.on[lane] & bit) {
        c.fillRect(step * kOverviewStepW + 1, y + 1, kOverviewStepW - 2, kOverviewRowH - 2,
                   laneColor(lane, p.accent[lane] & bit));
      }
    }
  }
  if (position >= 0) {
    c.drawRect(position * kOverviewStepW, kOverviewY, kOverviewStepW, DrumMachine::kLanes * kOverviewRowH,
               TFT_WHITE);
  }

  char text[32];
  snprintf(text, sizeof(text), "Pg %d/2", _padPage + 1);
  drawChip(c, 0, kStatusY, 64, kStatusH, text, _padPage == 1);
  drawChip(c, 64, kStatusY, 64, kStatusH, "Rec", _drums.recording(), TFT_RED);
  int queued = _drums.queuedPattern();
  if (queued >= 0) {
    snprintf(text, sizeof(text), "P%02d L%d >P%02d", pattern + 1, p.length, queued + 1);
  } else {
    snprintf(text, sizeof(text), "P%02d L%d %s", pattern + 1, p.length, following() && !_drums.enabled() ? "off" : "");
  }
  c.setTextColor(TFT_WHITE, TFT_BLACK);
  c.drawString(text, 136, kStatusY + 3);
}

void DrumPage::drawSound()
{
  M5Canvas &c = canvas();
  const DrumBank &bank = _drums.bank();
  const DrumLane &lane = bank.lanes[_lane];
  char value[32];
  const char *labels[4] = {"Note", "Vel", "Accent", "Pad ch"};
  for (int row = 0; row < 4; row++) {
    int y = kRowY[row];
    drawChip(c, 0, y, kButtonW, kRowH, "-", true);
    drawChip(c, 320 - kButtonW, y, kButtonW, kRowH, "+", true);
    switch (row) {
      case 0: snprintf(value, sizeof(value), "%u %s", lane.note, DrumKits::noteName(lane.note)); break;
      case 1: snprintf(value, sizeof(value), "%u", lane.velocity); break;
      case 2: snprintf(value, sizeof(value), "%u", lane.accentVelocity); break;
      default:
        if (bank.padChannel) snprintf(value, sizeof(value), "%u", bank.padChannel);
        else snprintf(value, sizeof(value), "off");
        break;
    }
    c.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    c.drawString(labels[row], kButtonW + 6, y + 4);
    c.setTextColor(TFT_WHITE, TFT_BLACK);
    c.drawString(value, kButtonW + 70, y + 4);
  }
  drawChip(c, 0, kSoundChipsY, 160, kStatusH, bank.usbOut ? "USB out: on" : "USB out: off", bank.usbOut);
  snprintf(value, sizeof(value), "Out ch %u", bank.channel);
  c.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  c.drawString(value, 170, kSoundChipsY + 3);
}

void DrumPage::drawPattern()
{
  M5Canvas &c = canvas();
  const DrumBank &bank = _drums.bank();
  int playing = _drums.playingPattern(), queued = _drums.queuedPattern();
  char text[48];
  for (int pattern = 0; pattern < DrumMachine::kPatterns; pattern++) {
    int x = kSlotX + (pattern % 4) * kSlotW, y = kSlotY + (pattern / 4) * (kSlotH + 2);
    bool content = false;
    for (int lane = 0; lane < DrumMachine::kLanes; lane++) content = content || bank.patterns[pattern].on[lane];
    uint16_t color = pattern == playing ? TFT_DARKGREEN : (pattern == queued ? TFT_OLIVE : 0x2945);
    c.fillRoundRect(x + 1, y + 1, kSlotW - 2, kSlotH - 2, 4, color);
    if (content) c.drawRoundRect(x + 1, y + 1, kSlotW - 2, kSlotH - 2, 4, TFT_CYAN);
    snprintf(text, sizeof(text), "%02d", pattern + 1);
    c.setTextColor(TFT_WHITE);
    c.setTextDatum(middle_center);
    c.drawString(text, x + kSlotW / 2, y + kSlotH / 2);
    c.setTextDatum(top_left);
  }

  int pattern = editPattern();
  c.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  c.drawString("Length", kSideX + 4, 28);
  drawChip(c, kSideX, 46, 40, 26, "-", true);
  snprintf(text, sizeof(text), "%d", bank.patterns[pattern].length);
  c.setTextColor(TFT_WHITE, TFT_BLACK);
  c.setTextDatum(middle_center);
  c.drawString(text, kSideX + 60, 59);
  c.setTextDatum(top_left);
  drawChip(c, 320 - 40, 46, 40, 26, "+", true);
  drawChip(c, kSideX, 80, 120, 24, _chainEdit ? "Chain: on" : "Chain: off", _chainEdit);
  drawChip(c, kSideX, 112, 120, 24, "Clear pattern", true, TFT_MAROON);

  int n = snprintf(text, sizeof(text), "Chain:");
  if (!bank.chainLength) {
    snprintf(text + n, sizeof(text) - n, " off");
  } else {
    for (int i = 0; i < bank.chainLength && n < (int)sizeof(text) - 4; i++) {
      n += snprintf(text + n, sizeof(text) - n, i == _drums.chainIndex() ? " [%02d]" : " %02d", bank.chain[i] + 1);
    }
  }
  c.setTextColor(TFT_WHITE, TFT_BLACK);
  c.drawString(text, 2, 176);
  c.setTextColor(TFT_DARKGREY, TFT_BLACK);
  c.drawString("Tap a slot: play it next", 2, 196);
}
