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
  void log(const MidiMessage &msg, bool forwarded, uint16_t totalLength);

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

  Line _lines[kLines];
  int _head = 0; // index of the next line to write
  int _count = 0;

  bool _paused = false;
  bool _showRealtime = false;
};

/*! Human-readable description of a message, e.g. "USB   1 NoteOn   C4   100".
 *  totalLength is the real SysEx length when msg holds only its first bytes. */
void describeMidi(const MidiMessage &msg, char *out, size_t size, uint16_t totalLength = 0);
