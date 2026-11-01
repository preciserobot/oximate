#include "router.h"

#include <Arduino.h>
#include <Preferences.h>

#include "midi_outputs.h"

static constexpr uint32_t kSaveDelayMillis = 2000; // batch rapid taps into one flash write
static const char *const kNamespace = "oximate";
static const char *const kKey = "router";

void Router::begin(MidiOutputs &outputs)
{
  _outputs = &outputs;
  load();
  apply();
}

void Router::apply()
{
  CriticalGuard guard(_lock);
  _active = _edit;
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
    case 0xF2: // song position
    case 0xF8: // clock
    case 0xFA: // start
    case 0xFB: // continue
    case 0xFC: // stop
      switch (config.clock) {
        case ClockSource::Both: return true;
        case ClockSource::Usb:  return msg.source == MidiSource::Usb;
        case ClockSource::Din:  return msg.source == MidiSource::Din;
        default:                return false;
      }
    default:
      return true;
  }
}

bool Router::route(const MidiMessage &msg)
{
  RouterConfig config;
  {
    CriticalGuard guard(_lock);
    config = _active;
  }
  if (!passes(config, msg)) {
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

  Origin origin = Origin::fromInput(msg.source);
  bool sent = false;
  switch (msg.source) {
    case MidiSource::Usb:
      if (config.usbToDin) {
        _outputs->send(MidiOutput::Din, out, origin);
        sent = true;
      }
      break;
    case MidiSource::Din:
      if (config.dinToDin) {
        _outputs->send(MidiOutput::Din, out, origin);
        sent = true;
      }
      if (config.dinToUsb && _outputs->available(MidiOutput::Usb)) {
        out.cable = 0;
        _outputs->send(MidiOutput::Usb, out, origin);
        sent = true;
      }
      break;
  }
  return sent;
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
  if (prefs.getBytesLength(kKey) == sizeof(stored) &&
      prefs.getBytes(kKey, &stored, sizeof(stored)) == sizeof(stored) &&
      stored.version == RouterConfig::kVersion) {
    _edit = stored;
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
