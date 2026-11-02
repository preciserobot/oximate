#include "ui.h"

using namespace Layout;

static constexpr uint32_t kFrameMillis = 40;
static constexpr int kHeaderTapHeight = kHeaderHeight + 4; // a little more than the bar: easier to hit
static constexpr int kSwipeMin = 140;          // pixels sideways for a page swipe (almost half the screen)
static constexpr uint32_t kSwipeMinMillis = 100; // shorter gestures are taps or touch jitter

static constexpr int kSlices = 8; // content pushed in slices of 25 rows (~3 ms of bus each)

void drawChip(LovyanGFX &target, int x, int y, int w, int h, const char *text, bool on, uint16_t onColor)
{
  target.fillRoundRect(x + 1, y + 1, w - 2, h - 2, 4, on ? onColor : kOffColor);
  uint16_t textColor = on ? (onColor == kOnColor ? TFT_WHITE : TFT_BLACK) : kOffTextColor;
  target.setTextColor(textColor);
  target.setTextDatum(middle_center);
  target.drawString(text, x + w / 2, y + h / 2);
  target.setTextDatum(top_left);
}
static constexpr uint32_t kHeaderMillis = 500;   // BPM readout changes without other events
static constexpr uint32_t kBatteryMillis = 5000; // battery level is read this often

void Ui::splash(const char *status)
{
  DisplayLock lock("splash");
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
  // Header and footer are drawn off-screen and pushed in one go: drawing text straight to the
  // display held the shared SPI bus for ~8 ms, delaying USB reads
  for (M5Canvas *bar : {&_header, &_footer}) {
    bar->setColorDepth(16);
    bar->setFont(&fonts::AsciiFont8x16);
    bar->setTextWrap(false);
  }
  _header.createSprite(M5.Display.width(), kHeaderHeight);
  _content.setPsram(true);
  _content.setColorDepth(16);
  _content.createSprite(M5.Display.width(), M5.Display.height());
  _content.setFont(&fonts::AsciiFont8x16);
  _content.setTextWrap(false);
  _content.fillScreen(TFT_BLACK);
  _footer.createSprite(M5.Display.width(), M5.Display.height() - kFooterTop);
  DisplayLock lock("begin");
  M5.Display.fillScreen(TFT_BLACK);
  M5.Display.setFont(&fonts::AsciiFont8x16);
  M5.Display.setTextWrap(false);
}

void Ui::addPage(Page &page)
{
  if (_pageCount < kMaxPages) {
    page.setCanvas(&_content);
    page.setUi(this);
    _pages[_pageCount++] = &page;
  }
}

/*! Push the content area from the canvas in slices, releasing the bus in between */
void Ui::pushContent()
{
  int height = kFooterTop - kContentTop;
  for (int i = 0; i < kSlices; i++) {
    int top = kContentTop + i * height / kSlices;
    int bottom = kContentTop + (i + 1) * height / kSlices;
    DisplayLock lock("content");
    M5.Display.setClipRect(0, top, M5.Display.width(), bottom - top);
    _content.pushSprite(0, 0);
    M5.Display.clearClipRect();
    if (_serviceHook) _serviceHook();
  }
}

void Ui::show(int index)
{
  _pages[_current]->leave();
  _current = index;
  _content.fillRect(0, kContentTop, _content.width(), kFooterTop - kContentTop, TFT_BLACK);
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
      if (_touchHook) _touchHook();
      page.onButton(i);
      _footerDirty = true;
    } else if (buttons[i]->wasHold()) {
      page.onButtonHold(i);
      _footerDirty = true;
    }
  }
  page.tick();
  if (page.takeFooterDirty()) _footerDirty = true;

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
    if (_touchHook) _touchHook();
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
    pushContent();
  }
  if (_footerDirty) {
    drawFooter();
    _footerDirty = false;
  }
}

void Ui::drawHeader()
{
  const char *usb = _usbConnected ? "USB ok" : "USB --"; // VID:PID is on the Diag page

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

  // Battery: read over I2C every few seconds only
  uint32_t now = millis();
  if (!_batteryReadAt || now - _batteryReadAt >= kBatteryMillis) {
    _batteryReadAt = now;
    _batteryLevel = M5.Power.getBatteryLevel();
    _charging = M5.Power.isCharging() == m5::Power_Class::is_charging;
  }

  // Columns: title 0-7, USB 9-14, clock 16-29; battery on the right
  char text[48];
  snprintf(text, sizeof(text), "%-8s %-6s %s", title, usb, clockText);
  M5Canvas &bar = _header;
  bar.fillScreen(kBarColor);
  bar.setTextColor(TFT_WHITE, kBarColor);
  bar.setCursor(0, (kHeaderHeight - kCharHeight) / 2);
  bar.print(text);
  drawBattery();
  if (_flashUntil) {
    const int x = 16 * kCharWidth; // where the clock text starts
    bar.fillRect(x, 0, bar.width() - x, kHeaderHeight, _flashColor);
    bar.setTextColor(TFT_WHITE, _flashColor);
    bar.setCursor(x + 4, (kHeaderHeight - kCharHeight) / 2);
    bar.print(_flashText);
  }
  DisplayLock lock("drawHeader");
  bar.pushSprite(0, 0);
}

/*! Small battery symbol plus percentage at the right end of the header (caller holds the lock) */
void Ui::drawBattery()
{
  const int textX = _header.width() - 4 * kCharWidth - 2; // "100%"
  const int iconX = textX - 24, iconY = 5, iconW = 18, iconH = 10;
  uint16_t color = _charging ? TFT_CYAN
                   : _batteryLevel < 0 ? TFT_LIGHTGREY
                   : _batteryLevel < 20 ? TFT_RED
                   : _batteryLevel <= 50 ? TFT_YELLOW
                                         : TFT_GREEN;
  _header.drawRect(iconX, iconY, iconW, iconH, color);
  _header.fillRect(iconX + iconW, iconY + 3, 2, iconH - 6, color); // terminal
  if (_batteryLevel > 0) {
    int fill = (iconW - 4) * (_batteryLevel > 100 ? 100 : _batteryLevel) / 100;
    _header.fillRect(iconX + 2, iconY + 2, fill < 1 ? 1 : fill, iconH - 4, color);
  }
  char percent[6];
  if (_batteryLevel < 0) {
    strcpy(percent, " --");
  } else {
    snprintf(percent, sizeof(percent), _charging ? "+%d" : "%3d", (int)_batteryLevel);
  }
  if (!_charging) strcat(percent, "%");
  _header.setTextColor(color, kBarColor);
  _header.setCursor(textX, (kHeaderHeight - kCharHeight) / 2);
  _header.print(percent);
}

void Ui::drawFooter()
{
  // Labels sit above the Core2 touch buttons A, B and C
  const char *labels[3] = {"", "", ""};
  _pages[_current]->buttonLabels(labels);
  const int centers[3] = {53, 160, 267};
  M5Canvas &bar = _footer;
  bar.fillScreen(kBarColor);
  bar.setTextColor(TFT_WHITE, kBarColor);
  bar.setTextDatum(top_center);
  for (int i = 0; i < 3; i++) {
    bar.drawString(labels[i], centers[i], 1);
  }
  bar.setTextDatum(top_left);
  DisplayLock lock("drawFooter");
  bar.pushSprite(0, kFooterTop);
}
