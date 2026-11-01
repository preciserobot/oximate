#pragma once

#include "clock_engine.h"
#include "leds.h"
#include "router.h"
#include "ui.h"

/*! Touch page for the router settings (layout in README "Router page") */
class RouterPage : public Page
{
public:
  RouterPage(Router &router, Leds &leds, ClockEngine &clock) : _router(router), _leds(leds), _clock(clock) {}

  const char *title() const override { return "Router"; }
  void buttonLabels(const char *labels[3]) const override;
  void onButton(int index) override;
  void onTouch(int x, int y) override;
  void draw() override;

private:
  void changed();

  Router &_router;
  Leds &_leds;
  ClockEngine &_clock;
};
