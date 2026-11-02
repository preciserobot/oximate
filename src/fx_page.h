#pragma once

#include "fx_engine.h"
#include "leds.h"
#include "ui.h"

/*! FX page (stage 11): the effect chain of each input.
 *  Tap an input tab to show its chain, a slot's box to switch it on or off, a slot's text to
 *  select it (tapping again steps through its settings). A / C = - / + (hold to repeat),
 *  B = bypass this input's chain, hold B = save both chains to the shown preset, tap the
 *  preset chip = load the next preset. */
class FxPage : public Page
{
public:
  FxPage(FxEngine &fx, Leds &leds) : _fx(fx), _leds(leds) {}

  const char *title() const override { return "FX"; }
  void buttonLabels(const char *labels[3]) const override;
  void onButton(int index) override;
  void onButtonHold(int index) override;
  void onTouch(int x, int y) override;
  void enter() override;
  void leave() override;
  void tick() override;
  void draw() override;

  struct Param
  {
    const char *name;
    int min, max, step;
  };
  /*! Settings of a slot, in order; returns the count */
  static int params(FxSlot slot, const Param **out);
  static int get(const FxChainConfig &c, FxSlot slot, int param);
  static void set(FxChainConfig &c, FxSlot slot, int param, int value);
  static void valueText(const FxChainConfig &c, FxSlot slot, int param, char *out, size_t size);
  static void summary(const FxChainConfig &c, FxSlot slot, char *out, size_t size);

private:
  static constexpr uint8_t kLedOwner = 11;

  FxChainConfig &chain() { return _fx.config().chains[_input]; }
  void adjust(int direction);
  void changed();
  void renderLeds();

  FxEngine &_fx;
  Leds &_leds;
  int _input = 0; ///< MidiSource shown
  int _slot = 0;
  int _param = 0;
  uint32_t _repeatAt = 0;
  uint32_t _lastDraw = 0;
  uint8_t _lastHeld = 0;
};
