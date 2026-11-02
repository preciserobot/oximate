#pragma once

#include <M5Unified.h>
#include "clock_engine.h"
#include "spi_bus.h"

/*! Holds the shared SPI bus and an M5GFX write transaction. Keep each one short (a few ms):
 *  the MIDI task skips USB polling while the display holds the bus. */
class DisplayLock
{
public:
  explicit DisplayLock(const char *holder = "display") : _bus(holder) { M5.Display.startWrite(); }
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

/*! A rounded, labelled touch target, drawn on `target` (normally the page's canvas) */
void drawChip(LovyanGFX &target, int x, int y, int w, int h, const char *text, bool on,
              uint16_t onColor = Layout::kOnColor);

class Ui;

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
  /*! Every input message drained from the MIDI task while this page is visible (UI thread;
   *  routing has already happened). For exact timing use an InputHooks observer instead. */
  virtual void onMidi(const MidiMessage &msg, bool forwarded) {}
  /*! Redraw the content area (only called when dirty()) on canvas(), a full-screen off-screen
   *  buffer: draw with the usual screen coordinates. The Ui pushes the content area afterwards,
   *  in slices, so the shared SPI bus is never held long. */
  virtual void draw() = 0;

  bool dirty() const { return _dirty; }
  void clean() { _dirty = false; }
  /*! True once after markFooterDirty(): the button labels changed on their own */
  bool takeFooterDirty()
  {
    bool dirty = _footerDirty;
    _footerDirty = false;
    return dirty;
  }

  void setCanvas(M5Canvas *canvas) { _canvas = canvas; }
  void setUi(Ui *ui) { _ui = ui; }

protected:
  void markDirty() { _dirty = true; }
  void markFooterDirty() { _footerDirty = true; }
  M5Canvas &canvas() { return *_canvas; }
  Ui *ui() { return _ui; } ///< e.g. for header messages (Ui::flash)

private:
  bool _dirty = true;
  bool _footerDirty = false;
  M5Canvas *_canvas = nullptr;
  Ui *_ui = nullptr;
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
  void onMidi(const MidiMessage &msg, bool forwarded) ///< pass to the visible page
  {
    if (_pageCount) _pages[_current]->onMidi(msg, forwarded);
  }
  void setUsbDevice(bool connected, uint16_t vid, uint16_t pid);
  /*! Called between screen slices, so time-critical UI work (haptic cues) isn't held up by a
   *  ~70 ms redraw, and on every touch button press or tap (touch tick) */
  void setHooks(void (*service)(), void (*touched)())
  {
    _serviceHook = service;
    _touchHook = touched;
  }
  /*! A short marker in the header (e.g. "SPK" while the built-in speaker is the output);
   *  nullptr hides it. Cheap to call every loop: redraws only on change. */
  void setStatusTag(const char *tag)
  {
    if (tag != _statusTag) {
      _statusTag = tag;
      _headerDirty = true;
    }
  }
  /*! Show a short message in the header's right side for `millis` (e.g. "PANIC") */
  void flash(const char *text, uint16_t color, uint32_t millis);

private:
  void show(int index);
  void drawHeader();
  void drawFooter();
  void drawBattery();

  const ClockEngine *_clock = nullptr;
  void pushContent();

  void (*_serviceHook)() = nullptr;
  void (*_touchHook)() = nullptr;

  M5Canvas _content{&M5.Display}; // full screen, in PSRAM; pages draw here
  M5Canvas _header{&M5.Display};
  M5Canvas _footer{&M5.Display};
  Page *_pages[kMaxPages] = {nullptr};
  int _pageCount = 0;
  int _current = 0;

  bool _usbConnected = false;
  uint16_t _usbVid = 0, _usbPid = 0;

  int32_t _batteryLevel = -1; // percent, -1 = unknown
  bool _charging = false;
  uint32_t _batteryReadAt = 0;

  const char *_statusTag = nullptr;
  char _flashText[20] = "";
  uint16_t _flashColor = TFT_WHITE;
  uint32_t _flashUntil = 0;

  bool _headerDirty = true, _footerDirty = true;
  uint32_t _lastFrame = 0, _lastHeader = 0;
};
