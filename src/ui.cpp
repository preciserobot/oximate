#include "ui.h"

using namespace Layout;

static constexpr uint32_t kFrameMillis = 40;
static constexpr uint32_t kHeaderMillis = 500; // BPM readout changes without other events

void Ui::begin(const ClockTracker &clock)
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

void Ui::poll()
{
  if (_pageCount == 0) {
    return;
  }
  Page &page = *_pages[_current];

  if (M5.BtnA.wasClicked()) { page.onButton(0); _footerDirty = true; }
  if (M5.BtnB.wasClicked()) { page.onButton(1); _footerDirty = true; }
  if (M5.BtnC.wasClicked()) { page.onButton(2); _footerDirty = true; }

  auto touch = M5.Touch.getDetail();
  if (touch.wasClicked() && touch.y < kFooterTop) {
    if (touch.y < kHeaderHeight) {
      show((_current + 1) % _pageCount);
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

  char clock[20] = "";
  MidiSource source;
  if (const ClockTracker::State *state = _clock->latest(source)) {
    snprintf(clock, sizeof(clock), "%s %5.1f %s", source == MidiSource::Usb ? "USB" : "DIN", state->bpm,
             state->running ? "PLAY" : "STOP");
  }

  // The ">" hints that tapping the header switches to the next page
  char title[10];
  snprintf(title, sizeof(title), "%s>", _pages[_current]->title());

  char text[48];
  snprintf(text, sizeof(text), "%-8s%-15s %s", title, usb, clock);
  DisplayLock lock;
  M5.Display.fillRect(0, 0, M5.Display.width(), kHeaderHeight, kBarColor);
  M5.Display.setTextColor(TFT_WHITE, kBarColor);
  M5.Display.setCursor(0, (kHeaderHeight - kCharHeight) / 2);
  M5.Display.print(text);
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
