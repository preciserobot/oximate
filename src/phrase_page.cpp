#include "phrase_page.h"

#include "platform_time.h"

using namespace Layout;

// Normal view rows
static constexpr int kCounterY = 28;
static constexpr int kProgressY = 86, kProgressH = 18;
static constexpr int kInfoY = 110;
static constexpr int kStatusY = 132;
static constexpr int kChipsY = 160, kChipH = 26;
static constexpr int kChips2Y = 190;
static constexpr int kChipW = 80;
static constexpr int kMargin = 10;

static constexpr uint32_t kFlashMillis = 150;
static constexpr uint32_t kBlinkMillis = 500;

static const uint8_t kPhraseLengths[] = {4, 8, 16, 32};
static const uint8_t kSectionLengths[] = {0, 2, 4, 8};
static const uint8_t kWarnLengths[] = {0, 1, 2, 4};

/*! The next value of `value` in `options` (wraps) */
template <size_t N>
static uint8_t nextOption(const uint8_t (&options)[N], uint8_t value)
{
  size_t i = 0;
  while (i < N && options[i] != value) i++;
  return options[(i + 1) % N];
}

void PhrasePage::buttonLabels(const char *labels[3]) const
{
  labels[0] = "Reset";
  labels[1] = "Bar 1";
  labels[2] = _big ? "Normal" : "Big";
}

void PhrasePage::onButton(int index)
{
  switch (index) {
    case 0:
      _tracker.reset();
      break;
    case 1:
      _tracker.realign(nowMicros64()); // rounded to the nearest bar line in the MIDI task
      break;
    case 2:
      _big = !_big;
      markDirty();
      break;
  }
}

void PhrasePage::onButtonHold(int index)
{
  if (index == 1) _tracker.realign(nowMicros64(), true); // restart the phrase, keep the section
}

void PhrasePage::changed()
{
  _tracker.changed();
  markDirty();
}

void PhrasePage::onTouch(int x, int y)
{
  if (_big) return;
  PhraseConfig &c = _tracker.config();
  if (y >= kChipsY && y < kChipsY + kChipH) {
    switch (x / kChipW) {
      case 0: c.phraseBars = nextOption(kPhraseLengths, c.phraseBars); break;
      case 1: c.sectionPhrases = nextOption(kSectionLengths, c.sectionPhrases); break;
      case 2: c.warnBars = nextOption(kWarnLengths, c.warnBars); break;
      default: c.sppRealigns = !c.sppRealigns; break;
    }
    changed();
  } else if (y >= kChips2Y && y < kChips2Y + kChipH && x < kChipW) {
    c.leds = c.leds ? 0 : 1;
    if (!c.leds) {
      _leds.release(kLedOwner);
      _claimed = false;
    }
    changed();
  }
}

void PhrasePage::enter()
{
  Page::enter();
  _visible = true;
}

void PhrasePage::leave()
{
  _visible = false;
  if (_claimed) _leds.release(kLedOwner);
  _claimed = false;
}

void PhrasePage::tick()
{
  PhraseSnapshot s = _tracker.snapshot();
  if (s.phraseStarts != _lastPhraseStarts) {
    _lastPhraseStarts = s.phraseStarts;
    _flashUntil = millis() + kFlashMillis;
  }
  renderLeds(s);
  bool changedView = s.bar != _shown.bar || s.running != _shown.running || s.counting != _shown.counting ||
                     s.synced != _shown.synced || s.phraseBars != _shown.phraseBars ||
                     s.sectionPhrases != _shown.sectionPhrases || s.warning != _shown.warning;
  if (changedView) markDirty();
}

void PhrasePage::renderLeds(const PhraseSnapshot &s)
{
  if (!_visible || !_tracker.config().leds) return;
  if (!_claimed) {
    _leds.claim(kLedOwner, 0, Leds::kCount);
    _claimed = true;
  }
  uint32_t now = millis();
  bool flash = s.synced && (int32_t)(_flashUntil - now) > 0;
  bool blinkOff = !s.synced && (now / kBlinkMillis) % 2; // not synced: the segments blink
  int lit = s.counting ? ((s.barInPhrase + 1) * Leds::kCount + s.phraseBars - 1) / s.phraseBars : 0;
  RGBColor on = s.warning ? RGBColor(255, 100, 0) : RGBColor(0, 60, 255);
  for (int i = 0; i < Leds::kCount; i++) {
    RGBColor color(0, 0, 0);
    if (flash) color = RGBColor(255, 255, 255);
    else if (i < lit && !blinkOff) color = on;
    _leds.setPixel(kLedOwner, i, color);
  }
}

void PhrasePage::draw()
{
  _shown = _tracker.snapshot();
  M5Canvas &c = canvas();
  c.fillRect(0, kContentTop, c.width(), kContentHeight, TFT_BLACK);
  if (_big) {
    drawBig(_shown);
  } else {
    drawNormal(_shown);
  }
}

/*! One segment per bar; the warning bars are orange, played bars filled */
void PhrasePage::drawProgress(const PhraseSnapshot &s, int y, int height)
{
  M5Canvas &c = canvas();
  int width = c.width() - 2 * kMargin;
  int warnFrom = s.phraseBars - (_tracker.config().warnBars < s.phraseBars ? _tracker.config().warnBars : 0);
  for (int b = 0; b < s.phraseBars; b++) {
    int x0 = kMargin + b * width / s.phraseBars;
    int x1 = kMargin + (b + 1) * width / s.phraseBars - 2;
    uint16_t color = b >= warnFrom ? TFT_ORANGE : TFT_BLUE;
    if (s.counting && b <= s.barInPhrase) {
      c.fillRect(x0, y, x1 - x0, height, color);
    } else {
      c.drawRect(x0, y, x1 - x0, height, b >= warnFrom ? TFT_ORANGE : TFT_DARKGREY);
    }
  }
}

void PhrasePage::drawNormal(const PhraseSnapshot &s)
{
  M5Canvas &c = canvas();
  const PhraseConfig &config = _tracker.config();
  char text[40];

  // Big counter: grey while stopped, orange in the warning bars, white otherwise
  uint16_t color = !s.counting ? TFT_DARKGREY : (s.warning ? TFT_ORANGE : TFT_WHITE);
  snprintf(text, sizeof(text), "%u", s.barInPhrase + 1);
  c.setFont(&fonts::Font7);
  c.setTextColor(color);
  c.setTextDatum(top_right);
  c.drawString(text, 170, kCounterY);
  c.setFont(&fonts::AsciiFont8x16);
  c.setTextDatum(top_left);
  c.setTextSize(2);
  snprintf(text, sizeof(text), "/%u", s.phraseBars);
  c.drawString(text, 178, kCounterY + 16);
  c.setTextSize(1);
  if (!s.synced) {
    c.setTextColor(TFT_RED);
    c.setTextSize(2);
    c.drawString("?", 280, kCounterY + 16);
    c.setTextSize(1);
  }

  drawProgress(s, kProgressY, kProgressH);

  c.setTextColor(s.counting ? TFT_WHITE : TFT_DARKGREY, TFT_BLACK);
  snprintf(text, sizeof(text), "%u bar%s left", s.barsLeft, s.barsLeft == 1 ? "" : "s");
  c.drawString(text, kMargin, kInfoY);
  if (s.sectionPhrases) {
    snprintf(text, sizeof(text), "phrase %u / %u", s.phraseInSection + 1, s.sectionPhrases);
    c.setTextDatum(top_right);
    c.drawString(text, c.width() - kMargin, kInfoY);
    c.setTextDatum(top_left);
  }

  const char *status = "";
  if (!s.synced && s.counting) status = "Not synced: tap Bar 1 on a downbeat";
  else if (!s.counting) status = s.running ? "" : "Stopped: counts from Start";
  c.setTextColor(!s.synced ? TFT_RED : TFT_LIGHTGREY, TFT_BLACK);
  c.drawString(status, kMargin, kStatusY);

  snprintf(text, sizeof(text), "Len %u", config.phraseBars);
  drawChip(c, 0 * kChipW, kChipsY, kChipW, kChipH, text, true);
  if (config.sectionPhrases) snprintf(text, sizeof(text), "Sect %u", config.sectionPhrases);
  else strcpy(text, "Sect off");
  drawChip(c, 1 * kChipW, kChipsY, kChipW, kChipH, text, config.sectionPhrases);
  if (config.warnBars) snprintf(text, sizeof(text), "Warn %u", config.warnBars);
  else strcpy(text, "Warn off");
  drawChip(c, 2 * kChipW, kChipsY, kChipW, kChipH, text, config.warnBars);
  drawChip(c, 3 * kChipW, kChipsY, kChipW, kChipH, config.sppRealigns ? "SPP on" : "SPP off", config.sppRealigns);
  drawChip(c, 0, kChips2Y, kChipW, kChipH, config.leds ? "LEDs on" : "LEDs off", config.leds);
}

void PhrasePage::drawBig(const PhraseSnapshot &s)
{
  M5Canvas &c = canvas();
  char text[8];
  uint16_t color = !s.counting ? TFT_DARKGREY : (s.warning ? TFT_ORANGE : TFT_WHITE);
  snprintf(text, sizeof(text), "%u", s.barInPhrase + 1);
  c.setFont(&fonts::Font7);
  c.setTextSize(2); // ~96 px tall digits, readable across a live table
  c.setTextColor(color);
  c.setTextDatum(top_center);
  c.drawString(text, c.width() / 2, kContentTop + 10);
  c.setTextSize(1);
  c.setFont(&fonts::AsciiFont8x16);
  c.setTextDatum(top_left);
  if (!s.synced) {
    c.setTextColor(TFT_RED);
    c.setTextSize(2);
    c.drawString("?", c.width() - 30, kContentTop + 10);
    c.setTextSize(1);
  }
  drawProgress(s, kContentTop + 130, 40);
}
