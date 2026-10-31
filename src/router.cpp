#include "router.h"

#include <Arduino.h>
#include <Preferences.h>

#include "din_midi.h"
#include "usb_midi.h"

static constexpr uint32_t kSaveDelayMillis = 2000; // batch rapid taps into one flash write
static const char *const kNamespace = "oximate";
static const char *const kKey = "router";

void Router::begin(DinMidi &din, UsbMidi &usb)
{
  _din = &din;
  _usb = &usb;
  load();
}

bool Router::passes(const MidiMessage &msg) const
{
  int input = (int)msg.source;
  if (msg.isChannel()) {
    return _config.channelMask[input] & (1 << (msg.channel() - 1));
  }
  switch (msg.status()) {
    case 0xF0:
      return _config.sysex;
    case 0xF2: // song position
    case 0xF8: // clock
    case 0xFA: // start
    case 0xFB: // continue
    case 0xFC: // stop
      switch (_config.clock) {
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
  if (!passes(msg)) {
    return false;
  }

  // Channel rewrite works on a copy; the input's buffer stays untouched
  uint8_t remapped[3];
  MidiMessage out = msg;
  uint8_t force = _config.forceChannel[(int)msg.source];
  if (msg.isChannel() && force) {
    memcpy(remapped, msg.bytes, msg.length);
    remapped[0] = (msg.status() & 0xF0) | (force - 1);
    out.bytes = remapped;
  }

  bool sent = false;
  switch (msg.source) {
    case MidiSource::Usb:
      if (_config.usbToDin) { _din->send(out); sent = true; }
      break;
    case MidiSource::Din:
      if (_config.dinToDin) { _din->send(out); sent = true; }
      if (_config.dinToUsb && _usb->connected()) {
        out.cable = 0;
        _usb->send(out);
        sent = true;
      }
      break;
  }
  return sent;
}

void Router::changed()
{
  _dirty = true;
  _changedMillis = millis();
}

void Router::resetDefaults()
{
  _config = RouterConfig();
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
    _config = stored;
  }
  prefs.end();
}

void Router::save()
{
  Preferences prefs;
  if (prefs.begin(kNamespace, false)) {
    prefs.putBytes(kKey, &_config, sizeof(_config));
    prefs.end();
  }
}
