#pragma once

#include "leds.h"
#include "scenes.h"
#include "ui.h"

/*! Set page (stage 15): live view of the setlist, scene editor and device setup.
 *  Live: A = previous, B = recall the armed scene (hold: now, ignoring its timing), C = next;
 *  tap "Now" / "Next" to edit that scene, "Devices" to set up the gear.
 *  Editor / Devices: tap a cell to select it, A / C = - / +, B = Done. */
class ScenePage : public Page
{
public:
  ScenePage(Scenes &scenes, Leds &leds) : _scenes(scenes), _leds(leds) {}

  const char *title() const override { return "Set"; }
  void buttonLabels(const char *labels[3]) const override;
  void onButton(int index) override;
  void onButtonHold(int index) override;
  void onTouch(int x, int y) override;
  void enter() override;
  void leave() override;
  void tick() override;
  void draw() override;

private:
  enum class View : uint8_t
  {
    Live,
    Editor,
    Devices,
  };
  static constexpr uint8_t kLedOwner = 15;

  void drawLive(const SceneStatus &status);
  void drawEditor();
  void drawDevices();
  void editorTouch(int x, int y);
  void devicesTouch(int x, int y);
  void adjust(int direction);
  void sceneLabel(int index, char *out, size_t size) const;
  void renderLeds(const SceneStatus &status);
  void message(const char *text);

  Scenes &_scenes;
  Leds &_leds;
  View _view = View::Live;
  int _editScene = 0;
  int _selectedRow = -1; ///< editor: device row; devices: device
  int _selectedCol = -1; ///< editor: 0 PC, 1 CCs; devices: 2 channel, 4 lead
  bool _visible = false;

  SceneStatus _shown = {};
  int _shownBeats = -1;
  uint32_t _lastDraw = 0;
  uint32_t _flashUntil = 0;
  uint32_t _lastFired = 0;
};
