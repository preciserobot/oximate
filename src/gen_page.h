#pragma once

#include "gen_engine.h"
#include "leds.h"
#include "ui.h"

/*! Gen page (stage 12): 8 generator slots.
 *  List view: tap a row's kind to cycle off / EUC / TUR / MOD, its mute chip to mute, its
 *  pattern to edit it. A / C = previous / next slot, B = mute (Turing: lock), hold B = reseed
 *  (Turing) or new rotation (Euclid), hold A = realign all slots on the next bar.
 *  Edit view: tap a setting, A / C = - / + (hold to repeat), B as above, Back chip. */
class GenPage : public Page
{
public:
  GenPage(GenEngine &gen, Leds &leds) : _gen(gen), _leds(leds) {}

  const char *title() const override { return "Gen"; }
  void buttonLabels(const char *labels[3]) const override;
  void onButton(int index) override;
  void onButtonHold(int index) override;
  void onTouch(int x, int y) override;
  void enter() override;
  void leave() override;
  void tick() override;
  void draw() override;

  enum Field : uint8_t
  {
    Out,
    Chan,
    Rate,
    Prob,
    Gate,
    Vel,
    Steps,
    Hits,
    Rot,
    Note,
    AccHits,
    AccRot,
    Len,
    Mutate,
    Scale,
    Root,
    Oct,
    Range,
    Density,
    Cc,
    Shape,
    Period,
    Min,
    Max,
  };

  /*! The settings a kind shows, in order; returns the count */
  static int fields(GenKind kind, const Field **out);
  static const char *fieldName(Field field);
  static void fieldText(const GenSlot &slot, Field field, char *out, size_t size);
  static void adjustField(GenSlot &slot, Field field, int direction);

private:
  static constexpr uint8_t kLedOwner = 12;

  void liveAction();
  void changed();
  void drawList();
  void drawEdit();
  void preview(int slot, char *out, size_t size) const;
  void renderLeds();

  GenEngine &_gen;
  Leds &_leds;
  int _slot = 0;
  bool _editing = false;
  int _field = 0;
  uint32_t _repeatAt = 0;
  uint32_t _lastDraw = 0;
};
