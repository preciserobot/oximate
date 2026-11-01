#include "sam_synth.h"

#include <Arduino.h>
#include <Preferences.h>
#include <string.h>

static constexpr uint32_t kSaveDelayMillis = 2000;
static const char *const kNamespace = "oximate";
static const char *const kKey = "synth";

// ---------------------------------------------------------------------------- encoding

uint16_t SamSynth::gsMessage(uint8_t *out, uint8_t address1, uint8_t address2, uint8_t address3, uint8_t value)
{
  // F0 41 <device 00> 42 12 <address> <data> <checksum> F7. The SAM2695 ignores the checksum,
  // but it is correct so GS gear downstream doesn't see malformed data
  uint8_t sum = address1 + address2 + address3 + value;
  const uint8_t message[11] = {0xF0, 0x41, 0x00, 0x42, 0x12, address1, address2, address3, value,
                               (uint8_t)((128 - (sum & 0x7F)) & 0x7F), 0xF7};
  memcpy(out, message, sizeof(message));
  return sizeof(message);
}

uint8_t SamSynth::voicesValue(SynthVoices voices)
{
  // NRPN 375Fh bits: 6 echo, 5 reverb, 4 chorus, 3 spatial, 2 mic, 1-0 equaliser
  switch (voices) {
    case SynthVoices::Full: return 0x30;
    case SynthVoices::Lite: return 0x10;
    default:                return 0x00;
  }
}

bool SamSynth::isReset(const MidiMessage &msg)
{
  const uint8_t *b = msg.bytes;
  if (msg.length == 1) return b[0] == 0xFF;
  if (msg.length == 6 && b[0] == 0xF0 && b[1] == 0x7E && b[3] == 0x09 && (b[4] == 0x01 || b[4] == 0x03)) {
    return true; // GM / GM2 system on
  }
  return msg.length == 11 && b[0] == 0xF0 && b[1] == 0x41 && b[3] == 0x42 && b[4] == 0x12 && b[5] == 0x40 &&
         b[6] == 0x00 && b[7] == 0x7F; // GS reset
}

// ---------------------------------------------------------------------------- UI side

void SamSynth::begin(MidiOutputs &outputs)
{
  _outputs = &outputs;
  outputs.addListener(onSent, this);
  load();
  _active = _pending = _edit;
  // The chip forgets everything when powered off. In All mode with default effects there is
  // nothing to restore, and nothing is sent at boot (external gear stays untouched).
  SynthConfig defaults;
  bool custom = _edit.listen != SynthListen::All || _edit.masterVolume != defaults.masterVolume ||
                _edit.reverbType != defaults.reverbType || _edit.reverbLevel != defaults.reverbLevel ||
                _edit.chorusLevel != defaults.chorusLevel || _edit.voices != defaults.voices;
  _restoreDue = custom;
}

void SamSynth::changed()
{
  {
    CriticalGuard guard(_lock);
    _pending = _edit;
    _pendingSet = true;
  }
  _dirty = true;
  _changedMillis = millis();
}

void SamSynth::poll()
{
  if (_dirty && millis() - _changedMillis >= kSaveDelayMillis) {
    save();
    _dirty = false;
  }
}

void SamSynth::resend()
{
  CriticalGuard guard(_lock);
  _restoreRequested = true;
}

void SamSynth::testLayer(int layer)
{
  CriticalGuard guard(_lock);
  _testRequested = layer;
}

void SamSynth::preview(int layer, uint8_t bank, uint8_t program)
{
  CriticalGuard guard(_lock);
  _previewLayer = layer;
  _previewBank = bank;
  _previewProgram = program;
}

void SamSynth::load()
{
  Preferences prefs;
  if (!prefs.begin(kNamespace, true)) return;
  SynthConfig stored;
  if (prefs.getBytesLength(kKey) == sizeof(stored) && prefs.getBytes(kKey, &stored, sizeof(stored)) == sizeof(stored) &&
      stored.version == SynthConfig::kVersion) {
    _edit = stored;
  }
  prefs.end();
}

void SamSynth::save()
{
  Preferences prefs;
  if (prefs.begin(kNamespace, false)) {
    prefs.putBytes(kKey, &_edit, sizeof(_edit));
    prefs.end();
  }
}

// ---------------------------------------------------------------------------- MIDI task side

void SamSynth::onSent(MidiOutput out, const MidiMessage &msg, Origin origin, void *ctx)
{
  SamSynth *synth = static_cast<SamSynth *>(ctx);
  if (out != MidiOutput::Din || (origin.kind == Origin::Internal && origin.owner == kOwner)) return;
  if (isReset(msg)) {
    synth->_restoreAt = millis() + kRestoreDelayMillis; // the chip is busy resetting meanwhile
    if (!synth->_restoreAt) synth->_restoreAt = 1;
  }
}

void SamSynth::send(const uint8_t *bytes, uint16_t length)
{
  _outputs->send(MidiOutput::Din, {MidiSource::Din, 0, bytes, length}, Origin::internal(kOwner));
}

void SamSynth::controlChange(uint8_t channel, uint8_t controller, uint8_t value)
{
  uint8_t bytes[3] = {(uint8_t)(0xB0 | (channel - 1)), controller, value};
  send(bytes, 3);
}

/*! NRPN on the config channel, then the null NRPN so later data entries do nothing */
void SamSynth::nrpn(uint8_t msb, uint8_t lsb, uint8_t value)
{
  uint8_t channel = _active.configChannel;
  controlChange(channel, 99, msb);
  controlChange(channel, 98, lsb);
  controlChange(channel, 6, value);
  controlChange(channel, 99, 127);
  controlChange(channel, 98, 127);
}

void SamSynth::gs(uint8_t address1, uint8_t address2, uint8_t address3, uint8_t value)
{
  uint8_t bytes[11];
  send(bytes, gsMessage(bytes, address1, address2, address3, value));
}

void SamSynth::assign(int part, uint8_t channel)
{
  gs(0x40, 0x10 | part, 0x02, channel ? channel - 1 : 16); // 16 = OFF: the part is muted
}

/*! Point the part at the config channel and set its sound there, where no external gear
 *  should be listening. The caller assigns it to its real channel afterwards. */
void SamSynth::configureOnConfigChannel(int part, const SynthLayer &layer, bool drums)
{
  uint8_t channel = _active.configChannel;
  assign(part, channel);
  gs(0x40, 0x10 | part, 0x15, drums ? 1 : 0);
  if (!drums) controlChange(channel, 0, layer.bank);
  uint8_t program[2] = {(uint8_t)(0xC0 | (channel - 1)), layer.program};
  send(program, 2);
  controlChange(channel, 7, layer.volume);
  controlChange(channel, 10, 64);
  controlChange(channel, 91, layer.reverb);
  controlChange(channel, 93, 0);
}

/*! The channel a part currently listens on */
uint8_t SamSynth::partChannel(int part) const
{
  for (int layer = 0; layer < kLayers; layer++) {
    if (partOf(layer) == part) return targetChannel(layer);
  }
  return _active.listen == SynthListen::All ? defaultChannel(part) : 0;
}

/*! Mute other parts that listen on the config channel (in All mode: part 15 on channel 16),
 *  so settings and auditions there don't reach them; returns them for unmuteParts() */
uint16_t SamSynth::muteConfigListeners(int exceptPart)
{
  uint16_t muted = 0;
  for (int part = 0; part < kParts; part++) {
    if (part != exceptPart && partChannel(part) == _active.configChannel) {
      assign(part, 0);
      muted |= 1 << part;
    }
  }
  return muted;
}

void SamSynth::unmuteParts(uint16_t muted)
{
  for (int part = 0; part < kParts; part++) {
    if (muted & (1 << part)) assign(part, partChannel(part));
  }
}

uint8_t SamSynth::targetChannel(int layer) const
{
  switch (_active.listen) {
    case SynthListen::All:    return defaultChannel(partOf(layer));
    case SynthListen::Layers: return _active.layers[layer].channel;
    default:                  return 0;
  }
}

void SamSynth::applyLayer(int layer)
{
  int part = partOf(layer);
  const SynthLayer &l = _active.layers[layer];
  if (_active.listen == SynthListen::Layers && l.channel) {
    uint16_t muted = muteConfigListeners(part);
    configureOnConfigChannel(part, l, layer == kDrumLayer);
    unmuteParts(muted);
  }
  assign(part, targetChannel(layer));
}

void SamSynth::applyGlobals()
{
  nrpn(0x37, 0x07, _active.masterVolume);
  nrpn(0x37, 0x5F, voicesValue(_active.voices));
  gs(0x40, 0x01, 0x30, _active.reverbType);
  gs(0x40, 0x01, 0x33, _active.reverbLevel);
  gs(0x40, 0x01, 0x3A, _active.chorusLevel);
}

void SamSynth::restore()
{
  if (_audition.active) endAudition();
  for (int part = 0; part < kParts; part++) {
    bool layerPart = part >= 0 && part <= 4;
    if (layerPart) continue; // set by applyLayer below
    assign(part, _active.listen == SynthListen::All ? defaultChannel(part) : 0);
  }
  if (_active.listen == SynthListen::All) gs(0x40, 0x10, 0x15, 1); // part 0 plays drums again
  for (int layer = 0; layer < kLayers; layer++) applyLayer(layer);
  applyGlobals();
}

void SamSynth::startAudition(int layer, uint8_t bank, uint8_t program, uint32_t nowMillis)
{
  if (_audition.active) endAudition();
  bool drums = layer == kDrumLayer;
  SynthLayer sound = _active.layers[layer];
  sound.bank = bank;
  sound.program = program;
  int part = partOf(layer);
  uint16_t muted = muteConfigListeners(part);
  configureOnConfigChannel(part, sound, drums);
  static const uint8_t kChord[3] = {60, 64, 67}; // C major
  static const uint8_t kDrums[3] = {36, 38, 42}; // kick, snare, closed hi-hat
  uint8_t status = 0x90 | (_active.configChannel - 1);
  for (uint8_t note : drums ? kDrums : kChord) {
    uint8_t on[3] = {status, note, 100};
    send(on, 3);
  }
  _audition = {true, part, layer, drums, nowMillis + kAuditionMillis, muted};
}

void SamSynth::endAudition()
{
  static const uint8_t kChord[3] = {60, 64, 67};
  static const uint8_t kDrums[3] = {36, 38, 42};
  uint8_t status = 0x80 | (_active.configChannel - 1);
  for (uint8_t note : _audition.drums ? kDrums : kChord) {
    uint8_t off[3] = {status, note, 0};
    send(off, 3);
  }
  _audition.active = false;
  applyLayer(_audition.returnLayer); // back to its saved sound and channel
  unmuteParts(_audition.muted);
}

void SamSynth::run(uint32_t nowMillis)
{
  SynthConfig next;
  bool configChanged, restoreRequested;
  int8_t test, previewLayer;
  uint8_t previewBank, previewProgram;
  {
    CriticalGuard guard(_lock);
    configChanged = _pendingSet;
    next = _pending;
    _pendingSet = false;
    restoreRequested = _restoreRequested;
    _restoreRequested = false;
    test = _testRequested;
    _testRequested = -1;
    previewLayer = _previewLayer;
    _previewLayer = -1;
    previewBank = _previewBank;
    previewProgram = _previewProgram;
  }

  if (configChanged) {
    SynthConfig previous = _active;
    _active = next;
    if (previous.listen != next.listen || previous.configChannel != next.configChannel) {
      restore();
    } else {
      for (int layer = 0; layer < kLayers; layer++) {
        if (memcmp(&previous.layers[layer], &next.layers[layer], sizeof(SynthLayer)) != 0) applyLayer(layer);
      }
      if (previous.masterVolume != next.masterVolume || previous.reverbType != next.reverbType ||
          previous.reverbLevel != next.reverbLevel || previous.chorusLevel != next.chorusLevel ||
          previous.voices != next.voices) {
        applyGlobals();
      }
    }
  }

  if (_restoreAt && (int32_t)(nowMillis - _restoreAt) >= 0) {
    _restoreAt = 0;
    _restoreDue = true;
  }
  if (restoreRequested || _restoreDue) {
    _restoreDue = false;
    restore();
  }

  if (_audition.active && (int32_t)(nowMillis - _audition.until) >= 0) {
    endAudition();
  }
  if (test >= 0 && test < kLayers) {
    const SynthLayer &l = _active.layers[test];
    startAudition(test, l.bank, l.program, nowMillis);
  } else if (previewLayer >= 0 && previewLayer < kLayers) {
    startAudition(previewLayer, previewBank, previewProgram, nowMillis);
  }
}
