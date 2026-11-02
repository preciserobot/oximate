#include "gen_engine.h"

#include <Arduino.h>
#include <Preferences.h>
#include <string.h>

#include "euclid.h"

static const char *const kNamespace = "oximate";
static const char *const kKey = "gen";
static constexpr uint32_t kMinGateMicros = 2000;

/*! Defaults until the user picks channels (README stage 12): everything starts muted, Euclid on
 *  the drum channel, Turing and the mod lane on channel 3 */
GenConfig::GenConfig()
{
  slots[0].kind = GenKind::Euclid; // kick-ish four on the floor
  slots[1].kind = GenKind::Euclid; // closed hats, 7 over 16
  slots[1].note = 42;
  slots[1].hits = 7;
  slots[1].rotation = 2;
  slots[1].velocity = 90;
  slots[2].kind = GenKind::Turing;
  slots[2].channel = 3;
  slots[2].seed = 0x5EED0003;
  slots[3].kind = GenKind::Mod;
  slots[3].channel = 3;
  for (int i = 0; i < kSlots; i++) {
    if (slots[i].seed == 1) slots[i].seed = 0x5EED0000 + i;
  }
}

int GenEngine::ticksPerStep(GenRate rate)
{
  switch (rate) {
    case GenRate::R32:  return 3;
    case GenRate::R8:   return 12;
    case GenRate::R4:   return 24;
    case GenRate::R16T: return 4;
    default:            return 6;
  }
}

void GenEngine::begin(MidiOutputs &outputs, ClockEngine &clock)
{
  _outputs = &outputs;
  _clock = &clock;
  _voice.begin(outputs, clock, kOwner);
  load();
  _active = _pending = _edit;
  for (int i = 0; i < kSlots; i++) {
    _turing[i].setLength(_active.slots[i].length);
    _turing[i].reseed(_active.slots[i].seed);
    _rng[i].seed(_active.slots[i].seed ^ 0xA5A5A5A5);
    _lastCc[i] = -1;
  }
  clock.addListener(onClock, this);
}

// ---------------------------------------------------------------------------- UI side

void GenEngine::changed()
{
  {
    CriticalGuard guard(_lock);
    _pending = _edit;
    _pendingSet = true;
  }
  _dirty = true;
  _changedMillis = millis();
}

void GenEngine::poll()
{
  // A flash write stalls both cores for a moment, so it waits until playback stops
  if (_dirty && !_playing && millis() - _changedMillis >= kSaveDelayMillis) {
    save();
    _dirty = false;
  }
}

void GenEngine::realign() { _realign = true; }

GenSlotStatus GenEngine::status(int slot) const
{
  CriticalGuard guard(_lock);
  return _status[slot];
}

void GenEngine::load()
{
  Preferences prefs;
  if (!prefs.begin(kNamespace, true)) return;
  GenConfig stored;
  if (prefs.getBytesLength(kKey) == sizeof(stored) && prefs.getBytes(kKey, &stored, sizeof(stored)) == sizeof(stored) &&
      stored.version == GenConfig::kVersion) {
    _edit = stored;
  }
  prefs.end();
}

void GenEngine::save()
{
  Preferences prefs;
  if (prefs.begin(kNamespace, false)) {
    prefs.putBytes(kKey, &_edit, sizeof(_edit));
    prefs.end();
  }
}

// ---------------------------------------------------------------------------- MIDI task side

void GenEngine::applyPending()
{
  GenConfig next;
  {
    CriticalGuard guard(_lock);
    if (!_pendingSet) return;
    next = _pending;
    _pendingSet = false;
  }
  for (int i = 0; i < kSlots; i++) {
    const GenSlot &before = _active.slots[i], &after = next.slots[i];
    if (after.length != before.length) _turing[i].setLength(after.length);
    if (after.seed != before.seed) _turing[i].reseed(after.seed); // hold B: reseed
    if (after.kind != before.kind || after.cc != before.cc) _lastCc[i] = -1;
  }
  _active = next;
}

void GenEngine::run(uint32_t) { applyPending(); }

void GenEngine::restart()
{
  for (int i = 0; i < kSlots; i++) {
    _position[i] = 0;
    _modTick[i] = 0;
    _turing[i].restart();
  }
}

/*! One step of a slot: Euclid hit or Turing note (Mod lanes are updated per tick) */
void GenEngine::trigger(int slot, uint64_t timeUs, uint32_t stepUs)
{
  const GenSlot &s = _active.slots[slot];
  uint32_t position = _position[slot]++;
  uint32_t gateUs = stepUs * s.gate / 100;
  if (gateUs < kMinGateMicros) gateUs = kMinGateMicros;
  int note = -1;
  uint8_t velocity = s.velocity;

  if (s.kind == GenKind::Euclid) {
    int steps = s.steps ? s.steps : 1;
    int step = position % steps;
    if (euclid(steps, s.hits, s.rotation) & (1u << step)) {
      note = s.note;
      if (euclid(steps, s.accentHits, s.accentRotation) & (1u << step)) {
        velocity = velocity + kAccentBoost > 127 ? 127 : velocity + kAccentBoost;
      }
    }
  } else if (s.kind == GenKind::Turing) {
    // The register advances (and mutates) even when muted, so unmuting joins a living loop
    uint8_t value = _turing[slot].step(s.locked ? 0 : s.mutate);
    note = turingNote(value, s.density, s.root % 12, MusicTheory::kScales[s.scale % MusicTheory::kScaleCount].mask,
                      s.octave, s.range);
  } else {
    return;
  }

  bool plays = note >= 0 && !s.muted && _rng[slot].chance(s.probability);
  if (plays) _voice.note(s.outputs, s.channel, note, velocity, timeUs, gateUs);

  CriticalGuard guard(_lock);
  GenSlotStatus &status = _status[slot];
  status.position = _position[slot];
  status.lastNote = plays ? note : -1;
  if (plays) status.triggers++;
  if (s.kind == GenKind::Turing) {
    for (int i = 0; i < TuringRegister::kMaxLength; i++) status.registerValues[i] = _turing[slot].value(i);
    status.registerPosition = _turing[slot].position();
  }
}

/*! Mod lane: called every clock tick; sends the CC only when its value changes */
void GenEngine::updateMod(int slot)
{
  const GenSlot &s = _active.slots[slot];
  uint32_t cycleTicks = (s.period ? s.period : 1) * 6u;
  uint32_t t = _modTick[slot]++ % cycleTicks;
  if (t == 0) { // new cycle: the smooth shape continues from where the last one ended
    _modFrom[slot] = s.shape == ModShape::SmoothRandom ? _modTo[slot] : (_rng[slot].next() % 1000) / 999.0f;
    _modTo[slot] = (_rng[slot].next() % 1000) / 999.0f;
  }
  uint8_t value = modValue(s.shape, (float)t / cycleTicks, _modFrom[slot], _modTo[slot], s.min, s.max);
  if (!s.muted && value != _lastCc[slot]) {
    _lastCc[slot] = value;
    _voice.controlChange(s.outputs, s.channel, s.cc & 0x7F, value);
  }
  CriticalGuard guard(_lock);
  _status[slot].modValue = value;
  _status[slot].position = _modTick[slot] / 6;
}

void GenEngine::onClock(const ClockEvent &event, void *ctx)
{
  GenEngine *gen = static_cast<GenEngine *>(ctx);
  gen->applyPending();
  switch (event.type) {
    case ClockEvent::Start:
      gen->_playing = true;
      gen->restart();
      break;
    case ClockEvent::Continue:
      gen->_playing = true;
      break;
    case ClockEvent::Stop:
      gen->_playing = false;
      gen->_voice.releaseAll(); // mod lanes stay where they are
      break;
    case ClockEvent::Tick: {
      ClockEngine::Snapshot snap = gen->_clock->snapshot();
      if (snap.bpm > 0) gen->_tickUs = (uint32_t)(60e6f / (24 * snap.bpm));
      for (int i = 0; i < kSlots; i++) {
        const GenSlot &s = gen->_active.slots[i];
        if (s.kind == GenKind::Mod) {
          gen->updateMod(i);
        } else if (s.kind != GenKind::Off && (s.rate == GenRate::R32 || s.rate == GenRate::R16T)) {
          int ticks = ticksPerStep(s.rate);
          if (event.tick % ticks == 0) gen->trigger(i, event.timeUs, ticks * gen->_tickUs);
        }
      }
      break;
    }
    case ClockEvent::Step:
      for (int i = 0; i < kSlots; i++) {
        const GenSlot &s = gen->_active.slots[i];
        if (s.kind != GenKind::Euclid && s.kind != GenKind::Turing) continue;
        if (s.rate == GenRate::R32 || s.rate == GenRate::R16T) continue;
        int every = ticksPerStep(s.rate) / 6; // 1/16 = every step, 1/8 = every 2nd, 1/4 = every 4th
        if (event.step % every == 0) gen->trigger(i, event.timeUs, ticksPerStep(s.rate) * gen->_tickUs);
      }
      break;
    case ClockEvent::Bar:
      // The bar's first step and tick have just played, so realigned slots continue with step 2
      if (gen->_realign.exchange(false)) {
        for (int i = 0; i < kSlots; i++) {
          gen->_position[i] = 1;
          gen->_modTick[i] = 1;
          gen->_turing[i].restart();
        }
      }
      break;
    default:
      break;
  }
}
