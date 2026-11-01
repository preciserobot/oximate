#include "ui.h"

using namespace Layout;

static constexpr uint32_t kFrameMillis = 40;
static constexpr int kHeaderTapHeight = kHeaderHeight + 4; // a little more than the bar: easier to hit
static constexpr int kSwipeMin = 140;          // pixels sideways for a page swipe (almost half the screen)
static constexpr uint32_t kSwipeMinMillis = 100; // shorter gestures are taps or touch jitter

void drawChip(int x, int y, int w, int h, const char *text, bool on, uint16_t onColor)
{
  M5.Display.fillRoundRect(x + 1, y + 1, w - 2, h - 2, 4, on ? onColor : kOffColor);
  uint16_t textColor = on ? (onColor == kOnColor ? TFT_WHITE : TFT_BLACK) : kOffTextColor;
  M5.Display.setTextColor(textColor);
  M5.Display.setTextDatum(middle_center);
  M5.Display.drawString(text, x + w / 2, y + h / 2);
  M5.Display.setTextDatum(top_left);
}
static constexpr uint32_t kHeaderMillis = 500; // BPM readout changes without other events

void Ui::splash(const char *status)
{
  DisplayLock lock;
  static bool drawn = false;
  int w = M5.Display.width(), h = M5.Display.height();
  if (!drawn) { // the banner once; later calls only update the status line
    drawn = true;
    // Same banner as the original firmware: square red box, built-in pixel font at 4x,
    // text sitting on the box's bottom edge
    M5.Display.fillScreen(TFT_BLACK);
    M5.Display.fillRect(30, 110, 260, 50, TFT_RED);
    M5.Display.setFont(&fonts::Font0);
    M5.Display.setTextSize(4);
    M5.Display.setTextColor(TFT_WHITE, TFT_RED);
    M5.Display.setCursor(40, 130);
    M5.Display.print("oximate");
    M5.Display.setTextSize(1);
  }
  M5.Display.setFont(&fonts::Font0);
  M5.Display.setTextSize(1);
  M5.Display.fillRect(0, h - 20, w, 12, TFT_BLACK);
  M5.Display.setTextColor(TFT_DARKGREY, TFT_BLACK);
  M5.Display.setTextDatum(top_center);
  M5.Display.drawString(status, w / 2, h - 18);
  M5.Display.setTextDatum(top_left);
}

void Ui::begin(const ClockEngine &clock)
{
  _clock = &clock;
  DisplayLock lock;
  M5.Display.fillScreen(TFT_BLACK);
  M5.Display.setFont(&fonts::AsciiFont8x16);
  M5.Display.setTextWrap(false);
}

void Ui::addPage(Page &page)
{
  if (_pageCount < kMaxPages) {
    _pages[_pageCount++] = &page;
  }
}

void Ui::show(int index)
{
  _pages[_current]->leave();
  _current = index;
  {
    DisplayLock lock;
    M5.Display.fillRect(0, kHeaderHeight, M5.Display.width(), kFooterTop - kHeaderHeight, TFT_BLACK);
  }
  _pages[_current]->enter();
  _headerDirty = _footerDirty = true;
}

void Ui::setUsbDevice(bool connected, uint16_t vid, uint16_t pid)
{
  if (connected != _usbConnected || vid != _usbVid || pid != _usbPid) {
    _usbConnected = connected;
    _usbVid = vid;
    _usbPid = pid;
    _headerDirty = true;
  }
}

void Ui::flash(const char *text, uint16_t color, uint32_t millisToShow)
{
  strncpy(_flashText, text, sizeof(_flashText) - 1);
  _flashColor = color;
  _flashUntil = millis() + millisToShow;
  _headerDirty = true;
}

void Ui::poll()
{
  if (_pageCount == 0) {
    return;
  }
  Page &page = *_pages[_current];

  m5::Button_Class *buttons[3] = {&M5.BtnA, &M5.BtnB, &M5.BtnC};
  for (int i = 0; i < 3; i++) {
    if (buttons[i]->wasClicked()) {
      page.onButton(i);
      _footerDirty = true;
    } else if (buttons[i]->wasHold()) {
      page.onButtonHold(i);
      _footerDirty = true;
    }
  }
  page.tick();

  auto touch = M5.Touch.getDetail();
  // Swipe left / right anywhere on the screen: next / previous page
  if ((touch.wasFlicked() || touch.wasDragged()) && touch.base_y < kFooterTop) {
    int dx = touch.distanceX(), dy = touch.distanceY();
    bool longEnough = millis() - touch.base_msec >= kSwipeMinMillis;
    if (abs(dx) >= kSwipeMin && abs(dx) > 3 * abs(dy) && longEnough) {
      show((_current + (dx < 0 ? 1 : _pageCount - 1)) % _pageCount);
      Serial.printf("Page: %s\n", _pages[_current]->title());
      return;
    }
  }
  if (touch.wasClicked() && touch.y < kFooterTop) {
    if (touch.y < kHeaderTapHeight) {
      show((_current + 1) % _pageCount);
      Serial.printf("Page: %s\n", _pages[_current]->title());
      return;
    }
    page.onTouch(touch.x, touch.y);
  }

  uint32_t now = millis();
  if (now - _lastFrame < kFrameMillis) {
    return;
  }
  if (now - _lastHeader >= kHeaderMillis) {
    _headerDirty = true;
  }
  if (_flashUntil && (int32_t)(now - _flashUntil) >= 0) {
    _flashUntil = 0; // flash over: back to the clock display
    _headerDirty = true;
  }
  if (!_headerDirty && !_footerDirty && !page.dirty()) {
    return;
  }
  _lastFrame = now;
  // Each part takes the bus separately, so USB polling can run between them
  if (_headerDirty) {
    drawHeader();
    _lastHeader = now;
    _headerDirty = false;
  }
  if (page.dirty()) {
    page.clean(); // before draw() so changes made while drawing trigger another frame
    page.draw();
  }
  if (_footerDirty) {
    drawFooter();
    _footerDirty = false;
  }
}

void Ui::drawHeader()
{
  char usb[16];
  if (_usbConnected) {
    snprintf(usb, sizeof(usb), "USB %04X:%04X", _usbVid, _usbPid);
  } else {
    strcpy(usb, "USB --");
  }

  static const char *const kModes[] = {"INT", "USB", "DIN"};
  ClockEngine::Snapshot clock = _clock->snapshot();
  char clockText[20];
  if (clock.mode != ClockMode::Internal && (clock.lost || clock.bpm <= 0)) {
    snprintf(clockText, sizeof(clockText), "%s --.- %s", kModes[(int)clock.mode], clock.lost ? "LOST" : "WAIT");
  } else {
    snprintf(clockText, sizeof(clockText), "%s %5.1f %s", kModes[(int)clock.mode], clock.bpm,
             clock.running ? "PLAY" : "STOP");
  }

  // The ">" hints that tapping the header switches to the next page
  char title[10];
  snprintf(title, sizeof(title), "%s>", _pages[_current]->title());

  char text[48];
  snprintf(text, sizeof(text), "%-8s%-15s %s", title, usb, clockText);
  DisplayLock lock;
  M5.Display.fillRect(0, 0, M5.Display.width(), kHeaderHeight, kBarColor);
  M5.Display.setTextColor(TFT_WHITE, kBarColor);
  M5.Display.setCursor(0, (kHeaderHeight - kCharHeight) / 2);
  M5.Display.print(text);
  if (_flashUntil) {
    const int x = 24 * kCharWidth; // where the clock text starts
    M5.Display.fillRect(x, 0, M5.Display.width() - x, kHeaderHeight, _flashColor);
    M5.Display.setTextColor(TFT_WHITE, _flashColor);
    M5.Display.setCursor(x + 4, (kHeaderHeight - kCharHeight) / 2);
    M5.Display.print(_flashText);
  }
}

void Ui::drawFooter()
{
  // Labels sit above the Core2 touch buttons A, B and C
  const char *labels[3] = {"", "", ""};
  _pages[_current]->buttonLabels(labels);
  const int centers[3] = {53, 160, 267};
  DisplayLock lock;
  M5.Display.fillRect(0, kFooterTop, M5.Display.width(), M5.Display.height() - kFooterTop, kBarColor);
  M5.Display.setTextColor(TFT_WHITE, kBarColor);
  M5.Display.setTextDatum(top_center);
  for (int i = 0; i < 3; i++) {
    M5.Display.drawString(labels[i], centers[i], kFooterTop + 1);
  }
  M5.Display.setTextDatum(top_left);
}
