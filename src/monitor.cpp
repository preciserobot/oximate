#include "monitor.h"

using namespace Layout;

static constexpr uint16_t kFilteredColor = TFT_DARKGREY;

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
  _canvas.setColorDepth(8);
  _canvas.createSprite(M5.Display.width(), kLines * kCharHeight);
  _canvas.setFont(&fonts::AsciiFont8x16);
  _canvas.setTextWrap(false);
}

void Monitor::log(const MidiMessage &msg, bool forwarded)
{
  if (_paused) {
    return;
  }
  // Clock and active sensing would flood the log; the header shows the clock instead
  if ((msg.status() == 0xF8 || msg.status() == 0xFE) && !_showRealtime) {
    return;
  }
  char text[kLineLength];
  describeMidi(msg, text, sizeof(text));
  uint16_t color = msg.source == MidiSource::Usb ? kUsbColor : kDinColor;
  addLine(forwarded ? color : kFilteredColor, text);
  Serial.println(text);
}

void Monitor::addLine(uint16_t color, const char *text)
{
  Line &line = _lines[_head];
  line.color = color;
  strncpy(line.text, text, kLineLength - 1);
  line.text[kLineLength - 1] = '\0';
  _head = (_head + 1) % kLines;
  if (_count < kLines) _count++;
  markDirty();
}

void Monitor::buttonLabels(const char *labels[3]) const
{
  labels[0] = "Clear";
  labels[1] = _paused ? "Resume" : "Pause";
  labels[2] = _showRealtime ? "Clock:on" : "Clock:off";
}

void Monitor::onButton(int index)
{
  switch (index) {
    case 0:
      _count = 0;
      _head = 0;
      markDirty();
      break;
    case 1:
      _paused = !_paused;
      break;
    case 2:
      _showRealtime = !_showRealtime;
      break;
  }
}

void Monitor::draw()
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
  _canvas.pushSprite(0, kContentTop);
}
