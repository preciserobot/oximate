#include "router.h"

#include <Arduino.h>
#include <Preferences.h>

#include "clock_engine.h"
#include "fx_engine.h"
#include "midi_outputs.h"
#include "note_tracker.h"

static constexpr uint32_t kSaveDelayMillis = 2000; // batch rapid taps into one flash write
static const char *const kNamespace = "oximate";
static const char *const kKey = "router";

/*! Layout of version 1 settings, for migration */
struct RouterConfigV1
{
  uint8_t version;
  bool usbToDin, dinToDin, dinToUsb;
  uint16_t channelMask[2];
  uint8_t forceChannel[2];
  ClockSource clock;
  bool sysex;
  uint8_t ledLevel;
};

/*! Layout of version 2 settings (stages 8-16), for migration */
struct RouterConfigV2
{
  uint8_t version;
  bool usbToDin, dinToDin, dinToUsb;
  uint16_t channelMask[2];
  uint8_t forceChannel[2];
  bool sysex;
  uint8_t ledLevel;
};

/*! The USB/DIN part of an older layout as a version 3 config, with the default radio routes */
static RouterConfig fromWired(bool usbToDin, bool dinToDin, bool dinToUsb, const uint16_t mask[2],
                              const uint8_t force[2], bool sysex, uint8_t ledLevel)
{
  RouterConfig c;
  c.setRouted(MidiSource::Usb, MidiOutput::Din, usbToDin);
  c.setRouted(MidiSource::Din, MidiOutput::Din, dinToDin);
  c.setRouted(MidiSource::Din, MidiOutput::Usb, dinToUsb);
  c.channelMask[0] = mask[0];
  c.channelMask[1] = mask[1];
  c.forceChannel[0] = force[0];
  c.forceChannel[1] = force[1];
  c.sysex = sysex;
  c.ledLevel = ledLevel;
  return c;
}

void Router::begin(MidiOutputs &outputs, ClockEngine &clock)
{
  _outputs = &outputs;
  _clock = &clock;
  load();
  _active = _edit;
}

void Router::setFx(FxEngine *fx)
{
  _fx = fx;
  if (fx) fx->setEmitter(emitFromFx, this);
}

/*! What the effects produce goes to the input's destinations, like plain thru */
void Router::emitFromFx(MidiSource input, const MidiMessage &msg, void *ctx)
{
  Router *router = static_cast<Router *>(ctx);
  MidiMessage out = msg;
  out.source = input;
  router->sendToDestinations(router->_active, out);
}

/*! UI: hand the edited settings to the MIDI task */
void Router::apply()
{
  CriticalGuard guard(_lock);
  _pending = _edit;
  _pendingSet = true;
}

bool Router::routingChanged(const RouterConfig &a, const RouterConfig &b, MidiSource input)
{
  int i = (int)input;
  return a.channelMask[i] != b.channelMask[i] || a.forceChannel[i] != b.forceChannel[i] ||
         a.routes[i] != b.routes[i];
}

void Router::applyPending()
{
  RouterConfig next;
  {
    CriticalGuard guard(_lock);
    if (!_pendingSet) return;
    next = _pending;
    _pendingSet = false;
  }
  // Notes started under the old routing get their note-offs now, on the same output and
  // channel, before the new settings could send the note-off somewhere else
  for (int i = 0; i < kInputs; i++) {
    MidiSource input = (MidiSource)i;
    if (routingChanged(_active, next, input)) {
      Origin origin = Origin::fromInput(input);
      for (int o = 0; o < kOutputs; o++) _outputs->tracker().release(*_outputs, (MidiOutput)o, &origin);
      if (_fx && isWired(input)) _fx->reset(input); // its remembered notes were just released
    }
  }
  _active = next;
}

bool Router::passes(const RouterConfig &config, const MidiMessage &msg)
{
  int input = (int)msg.source;
  if (msg.isChannel()) {
    return config.channelMask[input] & (1 << (msg.channel() - 1));
  }
  switch (msg.status()) {
    case 0xF0:
      return config.sysex;
    default:
      return true;
  }
}

bool Router::route(const MidiMessage &msg)
{
  applyPending();
  const RouterConfig &config = _active;
  switch (msg.status()) {
    case 0xF2: // song position
    case 0xF8: // clock
    case 0xFA: // start
    case 0xFB: // continue
    case 0xFC: // stop
      // The clock engine is the only sender of clock and transport; it uses them if it
      // follows this input
      return _clock->onExternal(msg);
  }
  if (!passes(config, msg)) {
    return false;
  }
  // A device on DIN echoing our own DIN output would loop forever through the software thru
  if (msg.source == MidiSource::Din && _outputs->isEcho(msg, msg.timeMicros ? msg.timeMicros : micros())) {
    return false;
  }

  // Channel rewrite works on a copy; the input's buffer stays untouched
  uint8_t remapped[3];
  MidiMessage out = msg;
  uint8_t force = config.forceChannel[(int)msg.source];
  if (msg.isChannel() && force) {
    memcpy(remapped, msg.bytes, msg.length);
    remapped[0] = (msg.status() & 0xF0) | (force - 1);
    out.bytes = remapped;
  }

  // Stage 11: the effect chain takes over while it has something to do; it sends what it
  // produces through sendToDestinations()
  if (_fx && isWired(msg.source) && out.isChannel() && _fx->engaged(msg.source)) {
    _fx->process(out);
    return hasDestination(config, msg.source);
  }
  return sendToDestinations(config, out);
}

/*! An input never goes back to its own USB device or radio peer (DIN -> DIN is the software thru) */
static bool ownPort(MidiSource input, MidiOutput out)
{
  return (input == MidiSource::Usb && out == MidiOutput::Usb) || (input == MidiSource::Ble && out == MidiOutput::Ble) ||
         (input == MidiSource::Net && out == MidiOutput::Net);
}

bool Router::hasDestination(const RouterConfig &config, MidiSource input) const
{
  for (int o = 0; o < kOutputs; o++) {
    MidiOutput out = (MidiOutput)o;
    if (out == MidiOutput::Din && config.routed(input, out)) return true; // DIN is always there
    if (config.routed(input, out) && !ownPort(input, out) && _outputs->available(out)) return true;
  }
  return false;
}

bool Router::sendToDestinations(const RouterConfig &config, MidiMessage out)
{
  Origin origin = Origin::fromInput(out.source);
  bool sent = false;
  uint8_t cable = out.cable;
  // In output order (DIN, USB, BLE, network): with the radios off this is exactly the old routing
  for (int o = 0; o < kOutputs; o++) {
    MidiOutput destination = (MidiOutput)o;
    if (!config.routed(out.source, destination) || ownPort(out.source, destination)) continue;
    if (!_outputs->available(destination)) continue;
    out.cable = destination == MidiOutput::Usb ? 0 : cable;
    _outputs->send(destination, out, origin);
    sent = true;
  }
  return sent;
}

bool Router::migratedClock(ClockSource &clock) const
{
  clock = _migratedClock;
  return _migrated;
}

void Router::changed()
{
  apply();
  _dirty = true;
  _changedMillis = millis();
}

void Router::resetDefaults()
{
  _edit = RouterConfig();
  changed();
}

void Router::poll()
{
  if (_dirty && millis() - _changedMillis >= kSaveDelayMillis) {
    save();
    _dirty = false;
  }
}

void Router::load()
{
  Preferences prefs;
  if (!prefs.begin(kNamespace, true)) {
    return; // nothing saved yet
  }
  RouterConfig stored;
  RouterConfigV2 v2;
  RouterConfigV1 old;
  size_t length = prefs.getBytesLength(kKey);
  if (length == sizeof(stored) && prefs.getBytes(kKey, &stored, sizeof(stored)) == sizeof(stored) &&
      stored.version == RouterConfig::kVersion) {
    _edit = stored;
  } else if (length == sizeof(v2) && prefs.getBytes(kKey, &v2, sizeof(v2)) == sizeof(v2) && v2.version == 2) {
    _edit = fromWired(v2.usbToDin, v2.dinToDin, v2.dinToUsb, v2.channelMask, v2.forceChannel, v2.sysex, v2.ledLevel);
    _dirty = true; // rewrite in the new layout
  } else if (length == sizeof(old) && prefs.getBytes(kKey, &old, sizeof(old)) == sizeof(old) && old.version == 1) {
    _edit = fromWired(old.usbToDin, old.dinToDin, old.dinToUsb, old.channelMask, old.forceChannel, old.sysex,
                      old.ledLevel);
    _migrated = true;
    _migratedClock = old.clock;
    _dirty = true; // rewrite in the new layout
  }
  prefs.end();
}

void Router::save()
{
  Preferences prefs;
  if (prefs.begin(kNamespace, false)) {
    prefs.putBytes(kKey, &_edit, sizeof(_edit));
    prefs.end();
  }
}
