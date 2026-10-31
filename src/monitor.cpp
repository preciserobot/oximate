#include "monitor.h"

static constexpr int kHeaderHeight = 20;
static constexpr int kLogTop = 22;
static constexpr int kFooterTop = 222;
static constexpr int kCharHeight = 16;

static constexpr uint16_t kUsbColor = TFT_CYAN;
static constexpr uint16_t kDinColor = TFT_ORANGE;
static constexpr uint16_t kBarColor = TFT_NAVY;

static const char *const kNoteNames[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

/*! Note name with MIDI note 60 = C4 */
static void noteName(uint8_t note, char *out, size_t size)
{
  snprintf(out, size, "%s%d", kNoteNames[note % 12], note / 12 - 1);
}

void describeMidi(const MidiMessage &msg, char *out, size_t size)
{
  char source[5];
  if (msg.source == MidiSource::Din) {
    strcpy(source, "DIN");
  } else if (msg.cable == 0) {
    strcpy(source, "USB");
  } else {
    snprintf(source, sizeof(source), "USB%d", msg.cable + 1);
  }

  char channel[3] = "--";
  if (msg.isChannel()) {
    snprintf(channel, sizeof(channel), "%d", msg.channel());
  }

  const uint8_t *b = msg.bytes;
  char body[32];
  char note[5];
  switch (msg.isChannel() ? (b[0] & 0xF0) : b[0]) {
    case 0x80: noteName(b[1], note, sizeof(note)); snprintf(body, sizeof(body), "NoteOff  %-4s %d", note, b[2]); break;
    case 0x90: noteName(b[1], note, sizeof(note)); snprintf(body, sizeof(body), "NoteOn   %-4s %d", note, b[2]); break;
    case 0xA0: noteName(b[1], note, sizeof(note)); snprintf(body, sizeof(body), "PolyAT   %-4s %d", note, b[2]); break;
    case 0xB0: snprintf(body, sizeof(body), "CC       %-4d %d", b[1], b[2]); break;
    case 0xC0: snprintf(body, sizeof(body), "Program  %d", b[1]); break;
    case 0xD0: snprintf(body, sizeof(body), "ChanAT   %d", b[1]); break;
    case 0xE0: snprintf(body, sizeof(body), "Bend     %+d", ((b[2] << 7) | b[1]) - 8192); break;
    case 0xF0: {
      int n = snprintf(body, sizeof(body), "SysEx %u:", msg.length);
      for (uint16_t i = 1; i < msg.length - 1 && n < (int)sizeof(body) - 3; i++) {
        n += snprintf(body + n, sizeof(body) - n, " %02X", b[i]);
      }
      break;
    }
    case 0xF1: snprintf(body, sizeof(body), "MTC      %02X", b[1]); break;
    case 0xF2: snprintf(body, sizeof(body), "SongPos  %d", (b[2] << 7) | b[1]); break;
    case 0xF3: snprintf(body, sizeof(body), "SongSel  %d", b[1]); break;
    case 0xF6: strcpy(body, "TuneRequest"); break;
    case 0xF8: strcpy(body, "Clock"); break;
    case 0xFA: strcpy(body, "Start"); break;
    case 0xFB: strcpy(body, "Continue"); break;
    case 0xFC: strcpy(body, "Stop"); break;
    case 0xFE: strcpy(body, "ActiveSensing"); break;
    case 0xFF: strcpy(body, "Reset"); break;
    default:   snprintf(body, sizeof(body), "Unknown  %02X", b[0]); break;
  }
  snprintf(out, size, "%-4s %2s %s", source, channel, body);
}

void Monitor::begin()
{
  M5.Display.fillScreen(TFT_BLACK);
  _canvas.setColorDepth(8);
  _canvas.createSprite(M5.Display.width(), kLines * kCharHeight);
  _canvas.setFont(&fonts::AsciiFont8x16);
  _canvas.setTextWrap(false);
  M5.Display.setFont(&fonts::AsciiFont8x16);
  M5.Display.setTextWrap(false);
}

void Monitor::log(const MidiMessage &msg)
{
  trackClock(msg);
  if (_paused) {
    return;
  }
  // Clock and active sensing would flood the log; they are summarised in the header instead
  if ((msg.status() == 0xF8 || msg.status() == 0xFE) && !_showRealtime) {
    return;
  }
  char text[kLineLength];
  describeMidi(msg, text, sizeof(text));
  addLine(msg.source == MidiSource::Usb ? kUsbColor : kDinColor, text);
  Serial.println(text);
}

void Monitor::trackClock(const MidiMessage &msg)
{
  ClockState &clock = _clock[(int)msg.source];
  uint32_t now = micros();
  switch (msg.status()) {
    case 0xF8:
      clock.lastTickMillis = millis();
      if (clock.ticks == 0) {
        clock.beatStartMicros = now;
      } else if (clock.ticks == 24) { // 24 ticks per quarter note
        clock.bpm = 60e6f / (now - clock.beatStartMicros);
        clock.beatStartMicros = now;
        clock.ticks = 0;
      }
      clock.ticks++;
      break;
    case 0xFA:
      clock.ticks = 0;
      clock.running = true;
      _headerDirty = true;
      break;
    case 0xFB:
      clock.running = true;
      _headerDirty = true;
      break;
    case 0xFC:
      clock.running = false;
      _headerDirty = true;
      break;
  }
}

void Monitor::addLine(uint16_t color, const char *text)
{
  Line &line = _lines[_head];
  line.color = color;
  strncpy(line.text, text, kLineLength - 1);
  line.text[kLineLength - 1] = '\0';
  _head = (_head + 1) % kLines;
  if (_count < kLines) _count++;
  _logDirty = true;
}

void Monitor::setUsbDevice(bool connected, uint16_t vid, uint16_t pid)
{
  if (connected != _usbConnected || vid != _usbVid || pid != _usbPid) {
    _usbConnected = connected;
    _usbVid = vid;
    _usbPid = pid;
    _headerDirty = true;
  }
}

void Monitor::clear()
{
  _count = 0;
  _head = 0;
  _logDirty = true;
}

void Monitor::togglePause()
{
  _paused = !_paused;
  _footerDirty = true;
}

void Monitor::toggleRealtime()
{
  _showRealtime = !_showRealtime;
  _footerDirty = true;
}

void Monitor::draw()
{
  uint32_t now = millis();
  if (now - _lastFrame < kFrameMillis) {
    return;
  }
  // The BPM readout changes without any line being logged
  if (now - _lastHeader >= kHeaderMillis) {
    _headerDirty = true;
  }
  if (!_logDirty && !_headerDirty && !_footerDirty) {
    return;
  }
  _lastFrame = now;
  M5.Display.startWrite();
  if (_headerDirty) {
    drawHeader();
    _lastHeader = now;
  }
  if (_logDirty) drawLog();
  if (_footerDirty) drawFooter();
  M5.Display.endWrite();
  _headerDirty = _logDirty = _footerDirty = false;
}

void Monitor::drawHeader()
{
  char usb[16];
  if (_usbConnected) {
    snprintf(usb, sizeof(usb), "USB %04X:%04X", _usbVid, _usbPid);
  } else {
    strcpy(usb, "USB --");
  }

  // Show the clock of whichever source ticked most recently
  char clock[20] = "";
  const ClockState *latest = nullptr;
  int latestSource = 0;
  for (int i = 0; i < 2; i++) {
    const ClockState &c = _clock[i];
    if (c.lastTickMillis && millis() - c.lastTickMillis < kClockTimeoutMillis &&
        (!latest || c.lastTickMillis > latest->lastTickMillis)) {
      latest = &c;
      latestSource = i;
    }
  }
  if (latest) {
    snprintf(clock, sizeof(clock), "%s %5.1f %s", latestSource == (int)MidiSource::Usb ? "USB" : "DIN",
             latest->bpm, latest->running ? "PLAY" : "STOP");
  }

  char text[kLineLength];
  snprintf(text, sizeof(text), "OxiMate %-15s %s", usb, clock);
  M5.Display.fillRect(0, 0, M5.Display.width(), kHeaderHeight, kBarColor);
  M5.Display.setTextColor(TFT_WHITE, kBarColor);
  M5.Display.setCursor(0, (kHeaderHeight - kCharHeight) / 2);
  M5.Display.print(text);
}

void Monitor::drawLog()
{
  _canvas.fillScreen(TFT_BLACK);
  // Oldest line at the top, newest at the bottom
  int first = (_head - _count + kLines) % kLines;
  for (int i = 0; i < _count; i++) {
    const Line &line = _lines[(first + i) % kLines];
    _canvas.setTextColor(line.color);
    _canvas.setCursor(0, (kLines - _count + i) * kCharHeight);
    _canvas.print(line.text);
  }
  _canvas.pushSprite(0, kLogTop);
}

void Monitor::drawFooter()
{
  // Labels sit above the Core2 touch buttons A, B and C
  const char *labels[3] = {"Clear", _paused ? "Resume" : "Pause", _showRealtime ? "Clock:on" : "Clock:off"};
  const int centers[3] = {53, 160, 267};
  M5.Display.fillRect(0, kFooterTop, M5.Display.width(), M5.Display.height() - kFooterTop, kBarColor);
  M5.Display.setTextColor(_paused ? TFT_YELLOW : TFT_WHITE, kBarColor);
  M5.Display.setTextDatum(top_center);
  for (int i = 0; i < 3; i++) {
    M5.Display.drawString(labels[i], centers[i], kFooterTop + 1);
  }
  M5.Display.setTextDatum(top_left);
}
