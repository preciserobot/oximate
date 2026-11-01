#pragma once

#include "sam_synth.h"
#include "ui.h"

/*! Synth page (stage 6): what the SAM2695 plays and how it sounds.
 *  Tap a cell to select it; A / C decrease / increase it (hold to repeat); B browses
 *  instruments (on an instrument cell) or plays a test chord (on a layer row). */
class SynthPage : public Page
{
public:
  explicit SynthPage(SamSynth &synth) : _synth(synth) {}

  const char *title() const override { return "Synth"; }
  void buttonLabels(const char *labels[3]) const override;
  void onButton(int index) override;
  void onTouch(int x, int y) override;
  void enter() override;
  void tick() override;
  void draw() override;

private:
  // Cells: row 0 = mode / config channel / voices, rows 1-5 = layers (channel, instrument,
  // volume, reverb), row 6 = master / reverb type / reverb level / chorus, row 7 = wiring / resend
  struct Cell
  {
    int8_t row, col;
  };

  void adjust(int direction);
  void changed();
  bool instrumentSelected() const { return _selected.row >= 1 && _selected.row <= 5 && _selected.col == 1; }
  int selectedLayer() const { return _selected.row >= 1 && _selected.row <= 5 ? _selected.row - 1 : -1; }
  void drawMain();
  void drawBrowser();
  void instrumentText(const SynthLayer &layer, bool drums, char *out, size_t size) const;
  void openBrowser(int layer);
  void browserTouch(int x, int y);

  SamSynth &_synth;
  Cell _selected = {1, 1};

  bool _browsing = false;
  int _browseLayer = 0;
  uint8_t _browseBank = 0;
  uint8_t _browseProgram = 0;
  uint8_t _browseFamily = 0;

  uint32_t _repeatAt = 0;
};
