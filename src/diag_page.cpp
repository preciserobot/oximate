#include "diag_page.h"

#include "din_midi.h"
#include "midi_outputs.h"
#include "usb_midi.h"

using namespace Layout;

static constexpr uint32_t kRedrawMillis = 250;
static const char *const kViewNames[] = {"View:ch", "View:cnt", "View:sys"};

void DiagPage::buttonLabels(const char *labels[3]) const
{
  labels[0] = "Reset";
  labels[1] = kViewNames[_view];
  labels[2] = "Panic";
}

void DiagPage::onButton(int index)
{
  switch (index) {
    case 0:
      _stats.requestReset();
      _loopMax = _loopMaxShown = 0;
      break;
    case 1:
      _view = (View)((_view + 1) % kViews);
      _clearAll = true;
      markDirty();
      break;
    case 2:
      _panic.request(2);
      break;
  }
}

void DiagPage::onButtonHold(int index)
{
  if (index == 1) _panic.requestTestNote(); // hold B: test note to DIN OUT and the synth
  if (index == 2) _panic.request(3);
}

void DiagPage::enter()
{
  Page::enter();
  _clearAll = true;
  _leds.claim(kLedOwner, 0, Leds::kCount);
}

void DiagPage::leave() { _leds.release(kLedOwner); }

void DiagPage::loopTime(uint32_t micros)
{
  _loopSum += micros;
  _loopCount++;
  if (micros > _loopMax) _loopMax = micros;
}

void DiagPage::tick()
{
  renderLeds();
  uint32_t now = millis();
  if (now - _rateAt >= 1000) {
    _rateAt = now;
    for (int t = 0; t < Stats::kTypes; t++) {
      uint32_t total = 0;
      for (int input = 0; input < 2; input++) {
        uint32_t count = _stats.count(input, t);
        total += count - _previous[input][t];
        _previous[input][t] = count;
      }
      _rate[t] = total;
    }
    _loopAvgShown = _loopCount ? _loopSum / _loopCount : 0;
    _loopMaxShown = _loopMax;
    _loopSum = _loopCount = 0;
  }
  if (now - _lastDraw >= kRedrawMillis) {
    _lastDraw = now;
    markDirty();
  }
}

/*! One 40-column text row; padded so it overwrites the old text without clearing (no flicker) */
void DiagPage::line(int row, const char *text, uint16_t color)
{
  char padded[41];
  snprintf(padded, sizeof(padded), "%-40s", text);
  M5.Display.setTextColor(color, TFT_BLACK);
  M5.Display.setCursor(0, kContentTop + 2 + row * kCharHeight);
  M5.Display.print(padded);
}

void DiagPage::draw()
{
  DisplayLock lock;
  if (_clearAll) {
    _clearAll = false;
    M5.Display.fillRect(0, kContentTop, M5.Display.width(), kContentHeight, TFT_BLACK);
  }
  switch (_view) {
    case Channels: drawChannels(); break;
    case Counters: drawCounters(); break;
    default:       drawSystem(); break;
  }
}

void DiagPage::drawChannels()
{
  line(0, " ch  1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6", TFT_LIGHTGREY);
  uint32_t now = millis();
  static const char *const kNames[] = {" USB", " DIN"};
  static const uint16_t kColors[] = {kUsbColor, kDinColor};
  for (int input = 0; input < 2; input++) {
    int y = kContentTop + 2 + (1 + input) * kCharHeight;
    M5.Display.setTextColor(kColors[input], TFT_BLACK);
    M5.Display.setCursor(0, y);
    M5.Display.print(kNames[input]);
    for (int ch = 0; ch < 16; ch++) {
      uint32_t seen = _stats.lastSeen(input, ch);
      bool active = seen && now - seen < kActiveMillis;
      M5.Display.setTextColor(active ? kColors[input] : TFT_DARKGREY, TFT_BLACK);
      M5.Display.setCursor((5 + ch * 2) * kCharWidth, y);
      M5.Display.print(active ? "#" : ".");
    }
  }

  Panic::Summary panic = _panic.summary();
  char text[48];
  snprintf(text, sizeof(text), " Held       DIN %-4u USB %u", panic.held[0], panic.held[1]);
  line(4, text);
  snprintf(text, sizeof(text), " Sustain    DIN %-2u ch USB %u ch", panic.sustain[0], panic.sustain[1]);
  line(5, text);
  snprintf(text, sizeof(text), " Released   %lu notes automatically", (unsigned long)panic.autoReleased);
  line(6, text);
  if (panic.active) {
    snprintf(text, sizeof(text), " PANIC level %u  %u%%", panic.level, panic.progress);
    line(8, text, TFT_RED);
  } else {
    snprintf(text, sizeof(text), " Panics     %lu", (unsigned long)panic.panics);
    line(8, text);
  }
}

void DiagPage::drawCounters()
{
  line(0, "               USB in   DIN in    /s", TFT_LIGHTGREY);
  static const char *const kRows[] = {" Notes", " CC", " Bend/AT/PC", " Clock/transp", " SysEx", " Other"};
  char text[48];
  for (int t = 0; t < Stats::kTypes; t++) {
    snprintf(text, sizeof(text), "%-14s%7lu  %7lu  %5lu", kRows[t], (unsigned long)_stats.count(0, t),
             (unsigned long)_stats.count(1, t), (unsigned long)_rate[t]);
    line(1 + t, text);
  }
  snprintf(text, sizeof(text), "%-14s%7lu  %7lu", " Filtered", (unsigned long)_stats.filtered(0),
           (unsigned long)_stats.filtered(1));
  line(7, text);
  const MidiOutputs::Counters &out = _outputs.counters();
  snprintf(text, sizeof(text), " Out DIN/USB   %lu / %lu msgs", (unsigned long)out.messages[0],
           (unsigned long)out.messages[1]);
  line(9, text);
}

void DiagPage::drawSystem()
{
  char text[48];
  uint32_t seconds = millis() / 1000;
  snprintf(text, sizeof(text), " Uptime    %02lu:%02lu:%02lu", (unsigned long)(seconds / 3600),
           (unsigned long)(seconds / 60 % 60), (unsigned long)(seconds % 60));
  line(0, text);
  snprintf(text, sizeof(text), " Heap      %lu KB free (min %lu KB)", (unsigned long)(ESP.getFreeHeap() / 1024),
           (unsigned long)(ESP.getMinFreeHeap() / 1024));
  line(1, text);
  snprintf(text, sizeof(text), " PSRAM     %lu KB free", (unsigned long)(ESP.getFreePsram() / 1024));
  line(2, text);
  static const char *const kCharging[] = {"", "charging", ""};
  snprintf(text, sizeof(text), " Battery   %ld%%  %.2f V  %s", (long)M5.Power.getBatteryLevel(),
           M5.Power.getBatteryVoltage() / 1000.0f, kCharging[M5.Power.isCharging()]);
  line(3, text);
  snprintf(text, sizeof(text), " DIN       rx %lu  tx %lu  drop %lu", (unsigned long)_din.counters().rxBytes,
           (unsigned long)_din.counters().txBytes, (unsigned long)_din.counters().txDropped);
  line(4, text);
  snprintf(text, sizeof(text), " USB       dropped %lu  bus busy %lu", (unsigned long)_usb.counters().txDropped,
           (unsigned long)_usb.counters().busBusy);
  line(5, text);
  snprintf(text, sizeof(text), " UI events dropped %lu", (unsigned long)_eventsDropped);
  line(6, text);
  snprintf(text, sizeof(text), " UI loop   avg %lu us  max %lu us", (unsigned long)_loopAvgShown,
           (unsigned long)_loopMaxShown);
  line(7, text);
}

void DiagPage::renderLeds()
{
  // Channels 1-10: cyan = USB, orange = DIN, white = both, within the last 250 ms
  uint32_t now = millis();
  for (int ch = 0; ch < Leds::kCount; ch++) {
    bool usb = _stats.lastSeen(0, ch) && now - _stats.lastSeen(0, ch) < kActiveMillis;
    bool din = _stats.lastSeen(1, ch) && now - _stats.lastSeen(1, ch) < kActiveMillis;
    RGBColor color(0, 0, 0);
    if (usb && din) color = RGBColor(255, 255, 255);
    else if (usb) color = RGBColor(0, 255, 255);
    else if (din) color = RGBColor(255, 128, 0);
    _leds.setPixel(kLedOwner, ch, color);
  }
}
