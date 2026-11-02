#include "fx_page.h"

#include "music_theory.h"

using namespace Layout;

static constexpr int kTabY = 24, kTabH = 22;
static constexpr int kRowY = 50, kRowH = 24;
static constexpr int kBoxW = 30;
static constexpr int kEditY = 198;
static constexpr uint32_t kRepeatDelayMillis = 400, kRepeatMillis = 60;
static constexpr uint32_t kRedrawMillis = 250;

static const char *const kSlotNames[(int)FxSlot::Count] = {"Transpose", "Scale", "Chord", "Arp", "Velocity", "CC"};
static const char *const kDirNames[] = {"nearest", "up", "down"};
static const char *const kChordNames[] = {"maj", "min", "7", "min7", "sus4", "5th", "oct", "diatonic"};
static const char *const kRateNames[] = {"1/4", "1/8", "1/16", "1/32"};
static const char *const kModeNames[] = {"up", "down", "up-down", "random", "played"};
static const char *const kCurveNames[] = {"linear", "soft", "hard", "fixed"};
static const RGBColor kSlotColors[(int)FxSlot::Count] = {
    RGBColor(255, 128, 0), RGBColor(0, 255, 0), RGBColor(0, 128, 255),
    RGBColor(255, 0, 255), RGBColor(255, 255, 0), RGBColor(0, 255, 255),
};

int FxPage::params(FxSlot slot, const Param **out)
{
  static const Param kTranspose[] = {{"Semitones", -24, 24, 1}};
  static const Param kScale[] = {{"Root", 0, 11, 1}, {"Scale", 0, MusicTheory::kScaleCount - 1, 1}, {"Direction", 0, 2, 1}};
  static const Param kChord[] = {{"Chord", 0, (int)FxChord::Count - 1, 1}, {"Inversion", 0, 3, 1}, {"Spread", 0, 1, 1}};
  static const Param kArp[] = {{"Rate", 0, (int)FxArpRate::Count - 1, 1}, {"Mode", 0, (int)FxArpMode::Count - 1, 1},
                               {"Octaves", 1, 4, 1}, {"Gate %", 10, 100, 10}, {"Latch", 0, 1, 1}};
  static const Param kVelocity[] = {{"Curve", 0, (int)FxVelCurve::Count - 1, 1}, {"Min", 1, 127, 1}, {"Max", 1, 127, 1},
                                    {"Fixed", 1, 127, 1}, {"Random +-", 0, 40, 1}};
  static const Param kCc[] = {{"Source CC", 0, 127, 1}, {"Target CC", 0, 127, 1}, {"Min", 0, 127, 1},
                              {"Max", 0, 127, 1}, {"Invert", 0, 1, 1}, {"Smooth x10ms", 0, 50, 1}};
  switch (slot) {
    case FxSlot::Transpose: *out = kTranspose; return 1;
    case FxSlot::Scale:     *out = kScale; return 3;
    case FxSlot::Chord:     *out = kChord; return 3;
    case FxSlot::Arp:       *out = kArp; return 5;
    case FxSlot::Velocity:  *out = kVelocity; return 5;
    default:                *out = kCc; return 6;
  }
}

int FxPage::get(const FxChainConfig &c, FxSlot slot, int param)
{
  switch (slot) {
    case FxSlot::Transpose: return c.transpose;
    case FxSlot::Scale:     return param == 0 ? c.scaleRoot : (param == 1 ? c.scaleId : (int)c.scaleDir);
    case FxSlot::Chord:     return param == 0 ? (int)c.chord : (param == 1 ? c.chordInversion : c.chordOpen);
    case FxSlot::Arp: {
      const int values[] = {(int)c.arpRate, (int)c.arpMode, c.arpOctaves, c.arpGate, c.arpLatch};
      return values[param];
    }
    case FxSlot::Velocity: {
      const int values[] = {(int)c.velCurve, c.velMin, c.velMax, c.velFixed, c.velRandom};
      return values[param];
    }
    default: {
      const int values[] = {c.ccSource, c.ccTarget, c.ccMin, c.ccMax, c.ccInvert, c.ccSmooth};
      return values[param];
    }
  }
}

void FxPage::set(FxChainConfig &c, FxSlot slot, int param, int value)
{
  switch (slot) {
    case FxSlot::Transpose: c.transpose = value; break;
    case FxSlot::Scale:
      if (param == 0) c.scaleRoot = value;
      else if (param == 1) c.scaleId = value;
      else c.scaleDir = (FxScaleDir)value;
      break;
    case FxSlot::Chord:
      if (param == 0) c.chord = (FxChord)value;
      else if (param == 1) c.chordInversion = value;
      else c.chordOpen = value;
      break;
    case FxSlot::Arp:
      switch (param) {
        case 0: c.arpRate = (FxArpRate)value; break;
        case 1: c.arpMode = (FxArpMode)value; break;
        case 2: c.arpOctaves = value; break;
        case 3: c.arpGate = value; break;
        default: c.arpLatch = value; break;
      }
      break;
    case FxSlot::Velocity:
      switch (param) {
        case 0: c.velCurve = (FxVelCurve)value; break;
        case 1: c.velMin = value; break;
        case 2: c.velMax = value; break;
        case 3: c.velFixed = value; break;
        default: c.velRandom = value; break;
      }
      break;
    default:
      switch (param) {
        case 0: c.ccSource = value; break;
        case 1: c.ccTarget = value; break;
        case 2: c.ccMin = value; break;
        case 3: c.ccMax = value; break;
        case 4: c.ccInvert = value; break;
        default: c.ccSmooth = value; break;
      }
      break;
  }
}

void FxPage::valueText(const FxChainConfig &c, FxSlot slot, int param, char *out, size_t size)
{
  int v = get(c, slot, param);
  switch (slot) {
    case FxSlot::Transpose: snprintf(out, size, "%+d", v); return;
    case FxSlot::Scale:
      if (param == 0) snprintf(out, size, "%s", MusicTheory::pitchClassName(v));
      else if (param == 1) snprintf(out, size, "%s", MusicTheory::kScales[v].name);
      else snprintf(out, size, "%s", kDirNames[v]);
      return;
    case FxSlot::Chord:
      if (param == 0) snprintf(out, size, "%s", kChordNames[v]);
      else if (param == 1) snprintf(out, size, "inv %d", v);
      else snprintf(out, size, "%s", v ? "open" : "close");
      return;
    case FxSlot::Arp:
      if (param == 0) snprintf(out, size, "%s", kRateNames[v]);
      else if (param == 1) snprintf(out, size, "%s", kModeNames[v]);
      else if (param == 2) snprintf(out, size, "%d oct", v);
      else if (param == 3) snprintf(out, size, "%d%%", v);
      else snprintf(out, size, "%s", v ? "latch" : "no latch");
      return;
    case FxSlot::Velocity:
      if (param == 0) snprintf(out, size, "%s", kCurveNames[v]);
      else snprintf(out, size, "%d", v);
      return;
    default:
      if (param == 4) snprintf(out, size, "%s", v ? "inverted" : "normal");
      else if (param == 5) snprintf(out, size, v ? "%d ms" : "off", v * 10);
      else snprintf(out, size, "%d", v);
      return;
  }
}

void FxPage::summary(const FxChainConfig &c, FxSlot slot, char *out, size_t size)
{
  char a[16], b[16], d[16];
  switch (slot) {
    case FxSlot::Transpose: valueText(c, slot, 0, out, size); break;
    case FxSlot::Scale:
      if (c.scaleId == 0) {
        snprintf(out, size, "chromatic (pick a scale)");
        break;
      }
      valueText(c, slot, 0, a, sizeof(a));
      valueText(c, slot, 2, d, sizeof(d));
      snprintf(out, size, "%s %s %s", a, MusicTheory::kScales[c.scaleId].name, d);
      break;
    case FxSlot::Chord:
      valueText(c, slot, 0, a, sizeof(a));
      valueText(c, slot, 2, d, sizeof(d));
      snprintf(out, size, "%s inv %d %s", a, c.chordInversion, d);
      break;
    case FxSlot::Arp:
      valueText(c, slot, 0, a, sizeof(a));
      valueText(c, slot, 1, b, sizeof(b));
      snprintf(out, size, "%s %s %do %d%%%s", a, b, c.arpOctaves, c.arpGate, c.arpLatch ? " L" : "");
      break;
    case FxSlot::Velocity:
      valueText(c, slot, 0, a, sizeof(a));
      if (c.velCurve == FxVelCurve::Fixed) snprintf(out, size, "fixed %d", c.velFixed);
      else snprintf(out, size, "%s %d-%d", a, c.velMin, c.velMax);
      break;
    default:
      snprintf(out, size, "%d>%d %d-%d%s", c.ccSource, c.ccTarget, c.ccMin, c.ccMax, c.ccInvert ? " inv" : "");
      if (c.ccSmooth) {
        size_t n = strlen(out);
        snprintf(out + n, size - n, " ~%dms", c.ccSmooth * 10);
      }
      break;
  }
}

void FxPage::buttonLabels(const char *labels[3]) const
{
  labels[0] = "-";
  labels[1] = _fx.config().chains[_input].bypass ? "Bypassed" : "Bypass";
  labels[2] = "+";
}

void FxPage::changed()
{
  _fx.changed();
  markDirty();
}

void FxPage::adjust(int direction)
{
  const Param *list;
  int count = params((FxSlot)_slot, &list);
  if (_param >= count) _param = 0;
  const Param &p = list[_param];
  int v = get(chain(), (FxSlot)_slot, _param) + direction * p.step;
  v = v < p.min ? p.min : (v > p.max ? p.max : v);
  set(chain(), (FxSlot)_slot, _param, v);
  changed();
}

void FxPage::onButton(int index)
{
  if (index == 0) {
    adjust(-1);
  } else if (index == 2) {
    adjust(+1);
  } else {
    chain().bypass = !chain().bypass;
    changed();
  }
}

void FxPage::onButtonHold(int index)
{
  if (index != 1) return;
  _fx.savePreset(_fx.config().preset);
  changed();
  char text[20];
  snprintf(text, sizeof(text), "Preset %d saved", _fx.config().preset + 1);
  if (ui()) ui()->flash(text, TFT_DARKGREEN, 1200);
}

void FxPage::onTouch(int x, int y)
{
  if (y >= kTabY && y < kTabY + kTabH) {
    if (x < 160) {
      _input = x / 80; // USB, DIN
    } else if (x < 250) {
      _fx.loadPreset((_fx.config().preset + 1) % FxConfig::kPresets);
      changed();
    }
    markDirty();
    return;
  }
  if (y >= kRowY && y < kRowY + (int)FxSlot::Count * kRowH) {
    int slot = (y - kRowY) / kRowH;
    if (x < kBoxW + 4) {
      chain().enabled ^= 1 << slot;
      changed();
      return;
    }
    if (slot == _slot) {
      const Param *list;
      _param = (_param + 1) % params((FxSlot)slot, &list); // tap again: next setting
    } else {
      _slot = slot;
      _param = 0;
    }
    markDirty();
  }
}

void FxPage::enter()
{
  Page::enter();
  _leds.claim(kLedOwner, 0, (int)FxSlot::Count);
}

void FxPage::leave() { _leds.release(kLedOwner); }

void FxPage::tick()
{
  m5::Button_Class *buttons[2] = {&M5.BtnA, &M5.BtnC};
  for (int i = 0; i < 2; i++) {
    if (buttons[i]->pressedFor(kRepeatDelayMillis) && millis() - _repeatAt >= kRepeatMillis) {
      _repeatAt = millis();
      adjust(i == 0 ? -1 : +1);
    }
  }
  uint8_t held = _fx.heldCount((MidiSource)_input);
  if (held != _lastHeld && millis() - _lastDraw >= kRedrawMillis) {
    _lastHeld = held;
    _lastDraw = millis();
    markDirty();
  }
  renderLeds();
}

void FxPage::renderLeds()
{
  const FxChainConfig &c = _fx.config().chains[_input];
  for (int slot = 0; slot < (int)FxSlot::Count; slot++) {
    _leds.setPixel(kLedOwner, slot, c.on((FxSlot)slot) ? kSlotColors[slot] : RGBColor(0, 0, 0));
  }
}

void FxPage::draw()
{
  M5Canvas &c = canvas();
  const FxConfig &config = _fx.config();
  const FxChainConfig &ch = config.chains[_input];
  c.fillRect(0, kContentTop, c.width(), kContentHeight, TFT_BLACK);

  drawChip(c, 0, kTabY, 80, kTabH, "USB in", _input == 0, kUsbColor);
  drawChip(c, 80, kTabY, 80, kTabH, "DIN in", _input == 1, kDinColor);
  char text[48];
  snprintf(text, sizeof(text), "Preset %d", config.preset + 1);
  drawChip(c, 160, kTabY, 90, kTabH, text, true);
  snprintf(text, sizeof(text), "held %u", _fx.heldCount((MidiSource)_input));
  c.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  c.setCursor(254, kTabY + 3);
  c.print(text);

  for (int slot = 0; slot < (int)FxSlot::Count; slot++) {
    int y = kRowY + slot * kRowH;
    bool on = ch.enabled & (1 << slot);
    drawChip(c, 0, y, kBoxW, kRowH - 2, on ? "x" : "", on && !ch.bypass);
    if (slot == _slot) c.fillRect(kBoxW + 2, y, c.width() - kBoxW - 2, kRowH - 2, 0x2945);
    uint16_t color = on && !ch.bypass ? TFT_WHITE : TFT_DARKGREY;
    c.setTextColor(color, slot == _slot ? 0x2945 : TFT_BLACK);
    char line[48], sum[32];
    summary(ch, (FxSlot)slot, sum, sizeof(sum));
    snprintf(line, sizeof(line), "%-10s%s", kSlotNames[slot], sum);
    c.setCursor(kBoxW + 6, y + 3);
    c.print(line);
  }

  const Param *list;
  int count = params((FxSlot)_slot, &list);
  if (_param >= count) _param = 0;
  char value[20];
  valueText(ch, (FxSlot)_slot, _param, value, sizeof(value));
  snprintf(text, sizeof(text), "%s %s: %s", kSlotNames[_slot], list[_param].name, value);
  c.setTextColor(ch.bypass ? TFT_ORANGE : TFT_CYAN, TFT_BLACK);
  c.setCursor(4, kEditY);
  c.print(ch.bypass ? "Chain bypassed (B)" : text);
}
