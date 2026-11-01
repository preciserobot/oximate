#include "analyser_page.h"

#include <math.h>

using namespace Layout;

static constexpr uint32_t kRedrawMillis = 250; // whole content push: keep it to 4 per second
static constexpr uint32_t kLedMillis = 50;
static constexpr int kPlotTop = kContentTop + 20, kPlotBottom = kFooterTop - 22;
static const char *const kViewNames[] = {"View:Stats", "View:Hist", "View:Drift", "View:Phase"};

// Timestamp resolution today (see README stage 9): DIN is stamped when the MIDI task reads the
// UART (every 1 ms); USB per transfer (1 ms frames, read every 1 ms)
static const char *const kResolution = "~1 ms";

static const char *const kStatusNames[] = {"--", "STABLE", "JITTERY", "BAD"};

void AnalyserPage::buttonLabels(const char *labels[3]) const
{
  labels[0] = "Reset";
  labels[1] = kViewNames[_view];
  labels[2] = _analyser.csv() ? "CSV on" : (_graphInput == MidiSource::Usb ? "Graph:USB" : "Graph:DIN");
}

void AnalyserPage::onButton(int index)
{
  switch (index) {
    case 0: _analyser.reset(); break;
    case 1: _view = (View)((_view + 1) % kViews); break;
    case 2: _graphInput = _graphInput == MidiSource::Usb ? MidiSource::Din : MidiSource::Usb; break;
  }
  markDirty();
}

void AnalyserPage::onButtonHold(int index)
{
  if (index == 2) {
    _analyser.setCsv(!_analyser.csv());
    if (_analyser.csv()) Serial.println("input,tick,time_us,interval_us,batched");
  }
}

void AnalyserPage::enter()
{
  Page::enter();
  _visible = true;
  _leds.claim(kLedOwner, 0, Leds::kCount);
}

void AnalyserPage::leave()
{
  _visible = false;
  _leds.release(kLedOwner);
}

void AnalyserPage::tick()
{
  uint32_t now = millis();
  if (now - _lastLeds >= kLedMillis) {
    _lastLeds = now;
    renderLeds();
  }
  if (now - _lastDraw >= kRedrawMillis) {
    _lastDraw = now;
    markDirty();
  }
}

void AnalyserPage::pollCsv()
{
  ClockAnalyser::CsvRecord record;
  char line[64];
  while (Serial.availableForWrite() > (int)sizeof(line) && _analyser.popCsv(record)) {
    snprintf(line, sizeof(line), "%s,%lu,%lu,%lu,%d", record.input == (uint8_t)MidiSource::Usb ? "USB" : "DIN",
             (unsigned long)record.tick, (unsigned long)record.timeMicros, (unsigned long)record.interval,
             record.batched ? 1 : 0);
    Serial.println(line);
  }
}

/*! Each side of the LED bar shows one input's status, pulsing brighter on every beat */
void AnalyserPage::renderLeds()
{
  if (!_visible) return;
  uint32_t now = millis();
  for (int side = 0; side < 2; side++) {
    MidiSource input = side == 0 ? MidiSource::Usb : MidiSource::Din; // right side = USB
    const ClockStats &stats = _analyser.copy(input);
    ClockStatsSnapshot s = stats.snapshot(now);
    static const RGBColor kColors[] = {RGBColor(0, 0, 0), RGBColor(0, 255, 0), RGBColor(255, 200, 0),
                                       RGBColor(255, 0, 0)};
    RGBColor color = kColors[s.status];
    bool beat = s.active && (s.ticks % 24) < 3;
    float level = beat ? 1.0f : 0.25f;
    RGBColor shown(color.R8() * level, color.G8() * level, color.B8() * level);
    for (int i = 0; i < Leds::kPerSide; i++) _leds.setPixel(kLedOwner, side * Leds::kPerSide + i, shown);
  }
}

void AnalyserPage::draw()
{
  M5Canvas &c = canvas();
  c.fillRect(0, kContentTop, c.width(), kFooterTop - kContentTop, TFT_BLACK);
  c.setTextDatum(top_left);
  switch (_view) {
    case Stats:     drawStats(); break;
    case Histogram: drawHistogram(); break;
    case Drift:     drawDrift(); break;
    default:        drawPhase(); break;
  }
}

void AnalyserPage::drawStats()
{
  M5Canvas &c = canvas();
  uint32_t now = millis();
  ClockStatsSnapshot s[2];
  s[0] = _analyser.copy(MidiSource::Usb).snapshot(now);
  s[1] = _analyser.copy(MidiSource::Din).snapshot(now);
  ClockPhase phase = _analyser.phase();

  int row = 0;
  auto line = [&](const char *label, const char *usb, const char *din, uint16_t color = TFT_WHITE) {
    char text[48];
    snprintf(text, sizeof(text), "%-13s%-13s%s", label, usb, din);
    c.setTextColor(color, TFT_BLACK);
    c.setCursor(0, kContentTop + 2 + row++ * kCharHeight);
    c.print(text);
  };
  char v[2][16];
  auto both = [&](const char *label, auto format) {
    for (int i = 0; i < 2; i++) {
      if (s[i].intervals) format(s[i], v[i]);
      else strcpy(v[i], "--");
    }
    line(label, v[0], v[1]);
  };

  line("", "USB", "DIN", TFT_LIGHTGREY);
  both("BPM now", [](const ClockStatsSnapshot &x, char *o) { snprintf(o, 16, "%.2f", x.bpmNow); });
  both("BPM avg 4b", [](const ClockStatsSnapshot &x, char *o) { snprintf(o, 16, "%.2f", x.bpmAvg); });
  both("Interval", [](const ClockStatsSnapshot &x, char *o) { snprintf(o, 16, "%.3f ms", x.intervalAvg); });
  both(" min/max", [](const ClockStatsSnapshot &x, char *o) {
    snprintf(o, 16, "%.2f/%.2f", x.intervalMin, x.intervalMax);
  });
  both("Jitter sd", [](const ClockStatsSnapshot &x, char *o) { snprintf(o, 16, "%.3f ms", x.jitterSd); });
  both("Max dev", [](const ClockStatsSnapshot &x, char *o) { snprintf(o, 16, "%+.2f ms", x.maxDeviation); });
  both("Dropouts", [](const ClockStatsSnapshot &x, char *o) {
    snprintf(o, 16, "%lu (%.0fms)", (unsigned long)x.dropouts, x.longestGap);
  });
  both("Ticks/run", [](const ClockStatsSnapshot &x, char *o) {
    snprintf(o, 16, "%lu%s", (unsigned long)x.ticksThisRun, x.batched ? " B" : "");
  });
  for (int i = 0; i < 2; i++) {
    if (!s[i].ticks) strcpy(v[i], "--");
    else snprintf(v[i], 16, "%s%s%s", s[i].running ? "PLAY" : "STOP", s[i].doubleClock ? " 2CLK" : "",
                  s[i].songPositionMismatch ? " SPP!" : "");
  }
  line("Transport", v[0], v[1]);

  char text[48];
  if (phase.valid()) snprintf(text, sizeof(text), "%+.2f ms sd %.2f", phase.meanMs(), phase.sdMs());
  else strcpy(text, "n/a (need both clocks)");
  line("Phase D-U", text, "");

  snprintf(text, sizeof(text), "Res %s", kResolution);
  uint16_t worst = TFT_WHITE;
  for (int i = 0; i < 2; i++) {
    strcpy(v[i], kStatusNames[s[i].status]);
    if (s[i].status == ClockStatsSnapshot::Bad) worst = TFT_RED;
    else if (s[i].status == ClockStatsSnapshot::Jittery && worst != TFT_RED) worst = TFT_YELLOW;
  }
  line(text, v[0], v[1], worst);
  if (_analyser.csv()) {
    c.setTextColor(TFT_CYAN, TFT_BLACK);
    c.setCursor(0, kContentTop + 2 + row * kCharHeight);
    c.print("CSV streaming on serial (hold C: off)");
  }
}

/*! Title line and frame for the graph views */
void AnalyserPage::plotArea(const char *title)
{
  M5Canvas &c = canvas();
  c.setTextColor(_graphInput == MidiSource::Usb ? kUsbColor : kDinColor, TFT_BLACK);
  c.setCursor(0, kContentTop + 2);
  c.print(title);
  c.drawFastHLine(0, kPlotBottom, c.width(), TFT_DARKGREY);
}

void AnalyserPage::drawHistogram()
{
  M5Canvas &c = canvas();
  const ClockStats &stats = _analyser.copy(_graphInput);
  ClockStatsSnapshot s = stats.snapshot(millis());
  static uint16_t bins[ClockStats::kBins];
  float range;
  int n = stats.histogram(bins, range);
  char title[48];
  snprintf(title, sizeof(title), "%s jitter n=%d sd %.2fms +-%.0fms", _graphInput == MidiSource::Usb ? "USB" : "DIN",
           n, s.jitterSd, range);
  plotArea(title);
  if (!n) return;
  uint16_t peak = 1;
  for (uint16_t b : bins) peak = b > peak ? b : peak;
  int width = c.width() / ClockStats::kBins;
  for (int i = 0; i < ClockStats::kBins; i++) {
    int h = (kPlotBottom - kPlotTop) * bins[i] / peak;
    if (h) c.fillRect(i * width, kPlotBottom - h, width - 1, h, i == ClockStats::kBins / 2 ? TFT_WHITE : TFT_GREEN);
  }
  char label[12];
  c.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  snprintf(label, sizeof(label), "-%.0fms", range);
  c.drawString(label, 0, kPlotBottom + 3);
  c.setTextDatum(top_center);
  c.drawString("0", c.width() / 2, kPlotBottom + 3);
  c.setTextDatum(top_right);
  snprintf(label, sizeof(label), "+%.0fms", range);
  c.drawString(label, c.width(), kPlotBottom + 3);
  c.setTextDatum(top_left);
}

/*! A line plot of `values` (oldest first) auto-ranged around their mean */
static void plotLine(M5Canvas &c, const float *values, const bool *markers, int n, float minRange, uint16_t color,
                     float &mean, float &range)
{
  mean = 0;
  for (int i = 0; i < n; i++) mean += values[i];
  mean = n ? mean / n : 0;
  range = minRange;
  for (int i = 0; i < n; i++) range = fmaxf(range, fabsf(values[i] - mean));
  int height = kPlotBottom - kPlotTop;
  auto y = [&](float v) { return kPlotTop + (int)((range - (v - mean)) / (2 * range) * height); };
  c.drawFastHLine(0, y(mean), c.width(), TFT_DARKGREY);
  for (int i = 1; i < n; i++) {
    int x0 = (i - 1) * c.width() / n, x1 = i * c.width() / n;
    c.drawLine(x0, y(values[i - 1]), x1, y(values[i]), color);
    if (markers && markers[i]) c.fillRect(x1 - 1, kPlotTop, 3, height, TFT_RED);
  }
}

void AnalyserPage::drawDrift()
{
  M5Canvas &c = canvas();
  const ClockStats &stats = _analyser.copy(_graphInput);
  ClockStatsSnapshot s = stats.snapshot(millis());
  static float bpm[ClockStats::kBeats];
  static bool dropout[ClockStats::kBeats];
  int n = stats.beats(bpm, dropout, ClockStats::kBeats);
  char title[48];
  snprintf(title, sizeof(title), "%s BPM/beat  %+.2f BPM/min  n=%d", _graphInput == MidiSource::Usb ? "USB" : "DIN",
           s.driftBpmPerMinute, n);
  plotArea(title);
  if (n < 2) return;
  float mean, range;
  plotLine(c, bpm, dropout, n, 0.5f, TFT_CYAN, mean, range);
  char label[32];
  snprintf(label, sizeof(label), "%.2f +-%.2f BPM (red: dropout)", mean, range);
  c.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  c.drawString(label, 0, kPlotBottom + 3);
}

void AnalyserPage::drawPhase()
{
  M5Canvas &c = canvas();
  ClockPhase phase = _analyser.phase();
  if (!phase.valid()) {
    plotArea("Phase DIN-USB");
    c.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    c.drawString("Needs clock on USB and DIN,", 0, kPlotTop + 20);
    c.drawString("at the same tempo (within 1%).", 0, kPlotTop + 38);
    return;
  }
  char title[48];
  snprintf(title, sizeof(title), "Phase DIN-USB %+.2fms sd %.2fms", phase.meanMs(), phase.sdMs());
  plotArea(title);
  static float history[ClockPhase::kHistory];
  int n = phase.history(history, ClockPhase::kHistory);
  if (n < 2) return;
  float mean, range;
  plotLine(c, history, nullptr, n, 0.5f, TFT_MAGENTA, mean, range);
  char label[32];
  snprintf(label, sizeof(label), "per beat, +-%.2f ms", range);
  c.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  c.drawString(label, 0, kPlotBottom + 3);
}
