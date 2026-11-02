#pragma once

#include "midi_message.h"
#include "sync.h"

class MidiOutputs;
class ClockEngine;
class FxEngine;

/*! Version 1 setting: which input's clock was forwarded. Only used to migrate old settings;
 *  since version 2 the clock engine owns clock and transport (see stage 8). */
enum class ClockSource : uint8_t
{
  Both = 0,
  Usb = 1,
  Din = 2,
  Off = 3,
};

/*! Routing settings, persisted in NVS. Bump kVersion when the layout changes. */
struct RouterConfig
{
  static constexpr uint8_t kVersion = 2;

  uint8_t version = kVersion;
  bool usbToDin = true;                     ///< USB device -> DIN OUT (+ SAM2695)
  bool dinToDin = true;                     ///< DIN IN -> DIN OUT (software thru)
  bool dinToUsb = true;                     ///< DIN IN -> USB device
  uint16_t channelMask[2] = {0xFFFF, 0xFFFF}; ///< per input (MidiSource), bit n = channel n+1 passes
  uint8_t forceChannel[2] = {0, 0};         ///< per input: 0 = keep, 1..16 = rewrite to this channel
  bool sysex = true;
  uint8_t ledLevel = 2;                     ///< 0 off, 1 low, 2 medium, 3 high
};

/*! Filters, remaps and forwards merged MIDI according to RouterConfig.
 *  route() runs in the MIDI task; config(), changed() and poll() belong to the UI. */
class Router
{
public:
  void begin(MidiOutputs &outputs, ClockEngine &clock);
  /*! Stage 11: MIDI effects between the channel rewrite and the outputs (optional) */
  void setFx(FxEngine *fx);

  /*! Forward a message; returns false if it was filtered out completely (MIDI task) */
  bool route(const MidiMessage &msg);

  RouterConfig &config() { return _edit; } ///< the UI's copy
  void changed();       ///< hand config() to the MIDI task; saved after a short delay
  void applyPending();  ///< MIDI task: take over changed settings (route() also does this)
  void resetDefaults();
  void poll();          ///< performs delayed saves (UI)

  /*! True if version 1 settings were migrated at boot; `clock` is their clock setting */
  bool migratedClock(ClockSource &clock) const;

private:
  static bool passes(const RouterConfig &config, const MidiMessage &msg);
  bool hasDestination(const RouterConfig &config, MidiSource input) const;
  bool sendToDestinations(const RouterConfig &config, MidiMessage out);
  static void emitFromFx(MidiSource input, const MidiMessage &msg, void *ctx);
  void apply();
  static bool routingChanged(const RouterConfig &a, const RouterConfig &b, MidiSource input);
  void load();
  void save();

  RouterConfig _edit;   // edited by the UI
  RouterConfig _active; // used by route() in the MIDI task
  RouterConfig _pending; // handed over under _lock
  bool _pendingSet = false;
  CriticalSection _lock;
  MidiOutputs *_outputs = nullptr;
  ClockEngine *_clock = nullptr;
  FxEngine *_fx = nullptr;
  bool _migrated = false;
  ClockSource _migratedClock = ClockSource::Both;
  uint32_t _changedMillis = 0;
  bool _dirty = false;
};
