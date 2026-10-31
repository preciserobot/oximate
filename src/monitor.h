#pragma once

#include <M5Unified.h>
#include "midi_message.h"

/*! Scrolling MIDI monitor on the Core2 display.
 *  Logging is cheap (formats into a ring buffer); drawing is throttled in draw(). */
class Monitor
{
public:
  void begin();
  void log(const MidiMessage &msg);
  void setUsbDevice(bool connected, uint16_t vid, uint16_t pid);
  void draw(); ///< call every loop; redraws at most every kFrameMillis

  void clear();
  void togglePause();
  void toggleRealtime();

private:
  static constexpr int kLines = 12;          // visible log lines (8x16 font, 40 columns)
  static constexpr int kLineLength = 41;
  static constexpr uint32_t kFrameMillis = 40;
  static constexpr uint32_t kHeaderMillis = 500;
  static constexpr uint32_t kClockTimeoutMillis = 2000;

  struct Line
  {
    uint16_t color;
    char text[kLineLength];
  };

  struct ClockState
  {
    uint32_t lastTickMillis = 0;
    uint32_t beatStartMicros = 0;
    uint8_t ticks = 0;
    float bpm = 0;
    bool running = false;
  };

  void trackClock(const MidiMessage &msg);
  void addLine(uint16_t color, const char *text);
  void drawHeader();
  void drawFooter();
  void drawLog();

  M5Canvas _canvas{&M5.Display};
  Line _lines[kLines];
  int _head = 0; // index of the next line to write
  int _count = 0;

  ClockState _clock[2]; // indexed by MidiSource
  bool _paused = false;
  bool _showRealtime = false;
  bool _usbConnected = false;
  uint16_t _usbVid = 0, _usbPid = 0;

  bool _logDirty = true, _headerDirty = true, _footerDirty = true;
  uint32_t _lastFrame = 0;
  uint32_t _lastHeader = 0;
};

/*! Human-readable description of a message, e.g. "USB   1 NoteOn   C4   100" */
void describeMidi(const MidiMessage &msg, char *out, size_t size);
