#pragma once

#include <M5Unified.h>
#include "clock_engine.h"
#include "spi_bus.h"

/*! Holds the shared SPI bus and an M5GFX write transaction. Keep each one short (a few ms):
 *  the MIDI task skips USB polling while the display holds the bus. */
class DisplayLock
{
public:
  DisplayLock() { M5.Display.startWrite(); }
  ~DisplayLock() { M5.Display.endWrite(); }

private:
  SpiLock _bus; // constructed before startWrite(), released after endWrite()
};

/*! Screen layout shared by all pages (320x240, 8x16 font = 40 columns) */
namespace Layout
{
constexpr int kCharWidth = 8;
constexpr int kCharHeight = 16;
constexpr int kHeaderHeight = 20;
constexpr int kContentTop = 22;
constexpr int kFooterTop = 222;
constexpr int kContentHeight = kFooterTop - kContentTop;

constexpr uint16_t kUsbColor = TFT_CYAN;
constexpr uint16_t kDinColor = TFT_ORANGE;
constexpr uint16_t kBarColor = TFT_NAVY;
constexpr uint16_t kOnColor = TFT_DARKGREEN;
constexpr uint16_t kOffColor = 0x2945; // dark grey
constexpr uint16_t kOffTextColor = TFT_LIGHTGREY;
} // namespace Layout

/*! A rounded, labelled touch target; the caller holds a DisplayLock */
void drawChip(int x, int y, int w, int h, const char *text, bool on, uint16_t onColor = Layout::kOnColor);

/*! One screen of the UI. Touch buttons A/B/C belong to the active page. */
class Page
{
public:
  virtual ~Page() = default;
  virtual const char *title() const = 0;
  virtual void buttonLabels(const char *labels[3]) const = 0;
  virtual void onButton(int index) = 0;     ///< 0 = A, 1 = B, 2 = C
  virtual void onButtonHold(int index) {}   ///< a touch button held (~0.5 s) instead of clicked
  virtual void onTouch(int x, int y) {}     ///< taps in the content area
  virtual void enter() { _dirty = true; }   ///< page became visible
  virtual void leave() {}                   ///< another page is about to be shown
  virtual void tick() {}                    ///< every loop while visible (animations, LEDs)
  virtual void draw() = 0;                  ///< redraw the content area (only called when dirty()); takes DisplayLock itself

  bool dirty() const { return _dirty; }
  void clean() { _dirty = false; }

protected:
  void markDirty() { _dirty = true; }

private:
  bool _dirty = true;
};

/*! Owns page switching (tap the header), the header/footer bars and redraw throttling */
class Ui
{
public:
  static constexpr int kMaxPages = 8;

  /*! Start-up screen: red box with the name, plus a status line (call before begin()) */
  static void splash(const char *status);
  void begin(const ClockEngine &clock);
  void addPage(Page &page);
  void poll();                ///< handle buttons and touch, redraw what changed
  void footerChanged() { _footerDirty = true; }
  void setUsbDevice(bool connected, uint16_t vid, uint16_t pid);
  /*! Show a short message in the header's right side for `millis` (e.g. "PANIC") */
  void flash(const char *text, uint16_t color, uint32_t millis);

private:
  void show(int index);
  void drawHeader();
  void drawFooter();
  void drawBattery();

  const ClockEngine *_clock = nullptr;
  Page *_pages[kMaxPages] = {nullptr};
  int _pageCount = 0;
  int _current = 0;

  bool _usbConnected = false;
  uint16_t _usbVid = 0, _usbPid = 0;

  int32_t _batteryLevel = -1; // percent, -1 = unknown
  bool _charging = false;
  uint32_t _batteryReadAt = 0;

  char _flashText[20] = "";
  uint16_t _flashColor = TFT_WHITE;
  uint32_t _flashUntil = 0;

  bool _headerDirty = true, _footerDirty = true;
  uint32_t _lastFrame = 0, _lastHeader = 0;
};
