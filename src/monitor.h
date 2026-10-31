#pragma once

#include <M5Unified.h>
#include "midi_message.h"
#include "ui.h"

/*! Scrolling MIDI monitor page.
 *  Logging is cheap (formats into a ring buffer); the Ui decides when to draw. */
class Monitor : public Page
{
public:
  void begin();
  void log(const MidiMessage &msg, bool forwarded);

  const char *title() const override { return "Monitor"; }
  void buttonLabels(const char *labels[3]) const override;
  void onButton(int index) override;
  void draw() override;

private:
  static constexpr int kLines = 12; // visible log lines (8x16 font, 40 columns)
  static constexpr int kLineLength = 41;

  struct Line
  {
    uint16_t color;
    char text[kLineLength];
  };

  void addLine(uint16_t color, const char *text);

  M5Canvas _canvas{&M5.Display};
  Line _lines[kLines];
  int _head = 0; // index of the next line to write
  int _count = 0;

  bool _paused = false;
  bool _showRealtime = false;
};

/*! Human-readable description of a message, e.g. "USB   1 NoteOn   C4   100" */
void describeMidi(const MidiMessage &msg, char *out, size_t size);
