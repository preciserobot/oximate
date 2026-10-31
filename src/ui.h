#pragma once

#include <M5Unified.h>
#include "clock_tracker.h"

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
} // namespace Layout

/*! One screen of the UI. Touch buttons A/B/C belong to the active page. */
class Page
{
public:
  virtual ~Page() = default;
  virtual const char *title() const = 0;
  virtual void buttonLabels(const char *labels[3]) const = 0;
  virtual void onButton(int index) = 0;     ///< 0 = A, 1 = B, 2 = C
  virtual void onTouch(int x, int y) {}     ///< taps in the content area
  virtual void enter() { _dirty = true; }   ///< page became visible
  virtual void draw() = 0;                  ///< redraw the content area (only called when dirty())

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

  void begin(const ClockTracker &clock);
  void addPage(Page &page);
  void poll();                ///< handle buttons and touch, redraw what changed
  void footerChanged() { _footerDirty = true; }
  void setUsbDevice(bool connected, uint16_t vid, uint16_t pid);

private:
  void show(int index);
  void drawHeader();
  void drawFooter();

  const ClockTracker *_clock = nullptr;
  Page *_pages[kMaxPages] = {nullptr};
  int _pageCount = 0;
  int _current = 0;

  bool _usbConnected = false;
  uint16_t _usbVid = 0, _usbPid = 0;

  bool _headerDirty = true, _footerDirty = true;
  uint32_t _lastFrame = 0, _lastHeader = 0;
};
