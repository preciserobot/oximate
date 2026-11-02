#include "fx_engine.h"

#include <Arduino.h>
#include <Preferences.h>
#include <math.h>
#include <string.h>

#include "music_theory.h"
#include "platform_time.h"

static const char *const kNamespace = "oximate";
static const char *const kKey = "fx";
static constexpr uint32_t kMinGateMicros = 2000;
static constexpr uint32_t kSmoothMicros = 10000; ///< CC smoothing sends at most every 10 ms

static uint8_t clampNote(int note) { return note < 0 ? 0 : (note > 127 ? 127 : note); }

// ---------------------------------------------------------------------------- pure helpers

uint8_t FxEngine::quantiseNote(uint8_t note, uint8_t root, uint8_t scaleId, FxScaleDir dir)
{
  if (scaleId == 0 || scaleId >= MusicTheory::kScaleCount) return note; // chromatic: unchanged
  uint16_t mask = MusicTheory::kScales[scaleId].mask;
  auto inScale = [&](int n) { return (mask >> ((n - root + 120) % 12)) & 1; };
  switch (dir) {
    case FxScaleDir::Up:
      for (int n = note; n <= 127; n++)
        if (inScale(n)) return n;
      break;
    case FxScaleDir::Down:
      for (int n = note; n >= 0; n--)
        if (inScale(n)) return n;
      break;
    default:
      return MusicTheory::quantise(note, root, mask);
  }
  return MusicTheory::quantise(note, root, mask); // nothing in range that way: nearest
}

int FxEngine::chordOffsets(const FxChainConfig &c, uint8_t root, int8_t *out)
{
  static const int8_t kShapes[(int)FxChord::Count][4] = {
      {0, 4, 7, -1}, {0, 3, 7, -1}, {0, 4, 7, 10}, {0, 3, 7, 10},
      {0, 5, 7, -1}, {0, 7, -1, -1}, {0, 12, -1, -1}, {0, 4, 7, -1},
  };
  int count = 0;
  if (c.chord == FxChord::Diatonic && c.scaleId > 0 && c.scaleId < MusicTheory::kScaleCount) {
    // Stack thirds inside the scale: the 2nd and 4th scale notes above the root
    uint16_t mask = MusicTheory::kScales[c.scaleId].mask;
    out[count++] = 0;
    int found = 0;
    for (int d = 1; d < 12 && count < 3; d++) {
      if ((mask >> ((root + d - c.scaleRoot + 120) % 12)) & 1) {
        found++;
        if (found == 2 || found == 4) out[count++] = d;
      }
    }
  } else {
    for (int i = 0; i < 4 && kShapes[(int)c.chord][i] >= 0; i++) out[count++] = kShapes[(int)c.chord][i];
  }
  // Inversions move the lowest notes up an octave; "open" lifts the middle note an octave
  int inversion = c.chordInversion < count ? c.chordInversion : count - 1;
  for (int i = 0; i < inversion; i++) {
    int8_t low = out[0] + 12;
    for (int j = 0; j < count - 1; j++) out[j] = out[j + 1];
    out[count - 1] = low;
  }
  if (c.chordOpen && count >= 3) out[1] += 12;
  for (int i = 1; i < count; i++) // ascending
    for (int j = i; j > 0 && out[j] < out[j - 1]; j--) {
      int8_t t = out[j];
      out[j] = out[j - 1];
      out[j - 1] = t;
    }
  return count;
}

uint8_t FxEngine::curveVelocity(const FxChainConfig &c, uint8_t velocity, uint32_t random)
{
  float out;
  if (c.velCurve == FxVelCurve::Fixed) {
    out = c.velFixed;
  } else {
    float x = velocity / 127.0f;
    if (c.velCurve == FxVelCurve::Soft) x = sqrtf(x);
    else if (c.velCurve == FxVelCurve::Hard) x = x * x;
    out = c.velMin + x * (c.velMax - c.velMin);
  }
  if (c.velRandom) out += (int)(random % (2 * c.velRandom + 1)) - (int)c.velRandom;
  int v = (int)lroundf(out);
  return v < 1 ? 1 : (v > 127 ? 127 : v);
}

uint8_t FxEngine::mapCc(const FxChainConfig &c, uint8_t value)
{
  float x = value / 127.0f;
  if (c.ccInvert) x = 1 - x;
  int v = (int)lroundf(c.ccMin + x * (c.ccMax - c.ccMin));
  return v < 0 ? 0 : (v > 127 ? 127 : v);
}

// ---------------------------------------------------------------------------- UI side

void FxEngine::begin(ClockEngine &clock)
{
  _clock = &clock;
  load();
  for (int i = 0; i < 2; i++) _active[i] = _pending[i] = _edit.chains[i];
  clock.addListener(onClock, this);
}

void FxEngine::setEmitter(Emit emit, void *ctx)
{
  _emit = emit;
  _emitCtx = ctx;
}

void FxEngine::changed()
{
  {
    CriticalGuard guard(_lock);
    _pending[0] = _edit.chains[0];
    _pending[1] = _edit.chains[1];
    _pendingSet = true;
  }
  _dirty = true;
  _changedMillis = millis();
}

void FxEngine::poll()
{
  // A flash write stalls both cores for a moment, so it waits until the transport stops
  if (_dirty && !_running && millis() - _changedMillis >= kSaveDelayMillis) {
    save();
    _dirty = false;
  }
}

void FxEngine::loadPreset(int preset)
{
  _edit.preset = preset;
  _edit.chains[0] = _edit.presets[preset][0];
  _edit.chains[1] = _edit.presets[preset][1];
}

void FxEngine::savePreset(int preset)
{
  _edit.preset = preset;
  _edit.presets[preset][0] = _edit.chains[0];
  _edit.presets[preset][1] = _edit.chains[1];
}

void FxEngine::load()
{
  Preferences prefs;
  if (!prefs.begin(kNamespace, true)) return;
  FxConfig stored;
  if (prefs.getBytesLength(kKey) == sizeof(stored) && prefs.getBytes(kKey, &stored, sizeof(stored)) == sizeof(stored) &&
      stored.version == FxConfig::kVersion) {
    _edit = stored;
  }
  prefs.end();
}

void FxEngine::save()
{
  Preferences prefs;
  if (prefs.begin(kNamespace, false)) {
    prefs.putBytes(kKey, &_edit, sizeof(_edit));
    prefs.end();
  }
}

// ---------------------------------------------------------------------------- MIDI task side

uint32_t FxEngine::random()
{
  _rng ^= _rng << 13;
  _rng ^= _rng >> 17;
  _rng ^= _rng << 5;
  return _rng;
}

void FxEngine::applyPending()
{
  FxChainConfig next[2];
  {
    CriticalGuard guard(_lock);
    if (!_pendingSet) return;
    next[0] = _pending[0];
    next[1] = _pending[1];
    _pendingSet = false;
  }
  for (int input = 0; input < 2; input++) {
    // Turning the arp off (or bypassing the chain) ends its note now; held keys mapped to the
    // arp then end silently. Everything else changes from the next note-on.
    if (_active[input].on(FxSlot::Arp) && !next[input].on(FxSlot::Arp)) {
      arpReleaseSounding(input);
      arpClear(input);
    } else if (_active[input].arpLatch && !next[input].arpLatch && _state[input].keysDown == 0) {
      arpClear(input); // latch off with no keys down: the latched notes stop
    }
    if (!next[input].on(FxSlot::Cc)) _state[input].ccTarget = _state[input].ccValue = -1;
    _active[input] = next[input];
  }
}

bool FxEngine::engaged(MidiSource input)
{
  applyPending();
  int i = (int)input;
  const FxChainConfig &c = _active[i];
  const ChainState &s = _state[i];
  if (!c.bypass && c.enabled) return true;
  if (s.mapped || s.arpCount) return true; // notes from earlier still need their note-offs
  for (const PendingOff &off : s.offs)
    if (off.used) return true;
  return false;
}

FxEngine::Mapping *FxEngine::findMapping(ChainState &s, uint8_t channel, uint8_t note)
{
  for (Mapping &m : s.map)
    if (m.channel == channel && m.inNote == note) return &m;
  return nullptr;
}

void FxEngine::emitNote(int input, uint8_t status, uint8_t channel, uint8_t note, uint8_t velocity)
{
  uint8_t bytes[3] = {(uint8_t)(status | channel), note, velocity};
  if (_emit) _emit((MidiSource)input, {(MidiSource)input, 0, bytes, 3}, _emitCtx);
}

void FxEngine::process(const MidiMessage &msg)
{
  int input = (int)msg.source;
  uint8_t type = msg.status() & 0xF0;
  if (type == 0x90 && msg.length == 3 && msg.bytes[2] > 0) {
    noteOn(input, msg);
  } else if ((type == 0x80 || type == 0x90) && msg.length == 3) {
    noteOff(input, msg);
  } else if (type == 0xB0 && msg.length == 3) {
    controlChange(input, msg);
  } else if (_emit) {
    _emit(msg.source, msg, _emitCtx); // program change, bend, aftertouch, SysEx: unchanged
  }
  _held[input] = _state[input].mapped;
}

void FxEngine::noteOn(int input, const MidiMessage &msg)
{
  const FxChainConfig &c = _active[input];
  ChainState &s = _state[input];
  uint8_t channel = msg.status() & 0x0F;
  uint8_t inNote = msg.bytes[1];
  if (findMapping(s, channel, inNote)) noteOff(input, msg); // repeated note-on: end the old one

  // transpose -> scale -> chord -> velocity (the arp picks the notes up from here)
  uint8_t root = inNote;
  if (c.on(FxSlot::Transpose)) root = clampNote(inNote + c.transpose);
  if (c.on(FxSlot::Scale)) root = quantiseNote(root, c.scaleRoot, c.scaleId, c.scaleDir);
  uint8_t notes[kMaxOut];
  int count = 0;
  if (c.on(FxSlot::Chord)) {
    int8_t offsets[kMaxOut];
    int n = chordOffsets(c, root, offsets);
    for (int i = 0; i < n && count < kMaxOut; i++) {
      uint8_t note = clampNote(root + offsets[i]);
      bool duplicate = false;
      for (int j = 0; j < count; j++) duplicate = duplicate || notes[j] == note;
      if (!duplicate) notes[count++] = note;
    }
  } else {
    notes[count++] = root;
  }
  uint8_t velocity = c.on(FxSlot::Velocity) ? curveVelocity(c, msg.bytes[2], random()) : msg.bytes[2];

  Mapping *m = nullptr; // a free slot (when all are taken the notes still play, unremembered)
  for (Mapping &candidate : s.map) {
    if (candidate.channel == 0xFF) {
      m = &candidate;
      break;
    }
  }
  bool arp = c.on(FxSlot::Arp) && _running; // stopped transport: notes pass through
  if (arp) {
    if (s.keysDown == 0 && c.arpLatch) arpClear(input); // a new chord replaces the latched one
    for (int i = 0; i < count; i++) {
      bool present = false;
      for (int j = 0; j < s.arpCount; j++) present = present || s.arp[j].note == notes[i];
      if (!present && s.arpCount < kArpNotes) {
        ArpNote &a = s.arp[s.arpCount++];
        a.note = notes[i];
        a.velocity = velocity;
        a.channel = channel;
        a.order = s.arpOrder++;
      }
    }
    s.keysDown++;
  } else {
    for (int i = 0; i < count; i++) emitNote(input, 0x90, channel, notes[i], velocity);
  }
  if (m) {
    m->channel = channel;
    m->inNote = inNote;
    m->count = count;
    m->arp = arp;
    memcpy(m->out, notes, count);
    s.mapped++;
  }
}

void FxEngine::noteOff(int input, const MidiMessage &msg)
{
  ChainState &s = _state[input];
  uint8_t channel = msg.status() & 0x0F;
  Mapping *m = findMapping(s, channel, msg.bytes[1]);
  if (!m) {
    if (_emit) _emit(msg.source, msg, _emitCtx); // started before the effects: unchanged
    return;
  }
  if (m->arp) {
    if (s.keysDown > 0) s.keysDown--;
    if (!_active[input].arpLatch) {
      for (int i = 0; i < m->count; i++) {
        for (int j = 0; j < s.arpCount; j++) {
          if (s.arp[j].note == m->out[i]) {
            s.arp[j] = s.arp[--s.arpCount];
            break;
          }
        }
      }
    }
  } else {
    for (int i = 0; i < m->count; i++) emitNote(input, 0x80, channel, m->out[i], 0);
  }
  m->channel = 0xFF;
  s.mapped--;
}

void FxEngine::controlChange(int input, const MidiMessage &msg)
{
  const FxChainConfig &c = _active[input];
  ChainState &s = _state[input];
  if (!c.on(FxSlot::Cc) || msg.bytes[1] != c.ccSource) {
    if (_emit) _emit(msg.source, msg, _emitCtx);
    return;
  }
  uint8_t value = mapCc(c, msg.bytes[2]);
  s.ccChannel = msg.status() & 0x0F;
  if (c.ccSmooth == 0 || s.ccValue < 0) {
    uint8_t bytes[3] = {(uint8_t)(0xB0 | s.ccChannel), c.ccTarget, value};
    if (_emit) _emit(msg.source, {msg.source, 0, bytes, 3}, _emitCtx);
    s.ccValue = value; // smoothing starts from here
  }
  s.ccTarget = value; // run() slews towards it
}

// ---------------------------------------------------------------------------- arpeggiator

void FxEngine::arpPlay(int input, uint64_t timeUs, uint32_t lengthUs)
{
  const FxChainConfig &c = _active[input];
  ChainState &s = _state[input];
  if (s.arpCount == 0) return;

  // The note set, ascending (or in the order played), repeated over the octaves
  ArpNote sorted[kArpNotes];
  memcpy(sorted, s.arp, s.arpCount * sizeof(ArpNote));
  for (int i = 1; i < s.arpCount; i++) {
    for (int j = i; j > 0; j--) {
      bool swap = c.arpMode == FxArpMode::Played ? sorted[j].order < sorted[j - 1].order
                                                 : sorted[j].note < sorted[j - 1].note;
      if (!swap) break;
      ArpNote t = sorted[j];
      sorted[j] = sorted[j - 1];
      sorted[j - 1] = t;
    }
  }
  int octaves = c.arpOctaves < 1 ? 1 : (c.arpOctaves > 4 ? 4 : c.arpOctaves);
  int length = s.arpCount * octaves;
  int position;
  switch (c.arpMode) {
    case FxArpMode::Down:
      position = length - 1 - (s.arpIndex % length);
      s.arpIndex++;
      break;
    case FxArpMode::UpDown: {
      int cycle = length > 1 ? 2 * length - 2 : 1;
      int k = s.arpIndex % cycle;
      position = k < length ? k : cycle - k;
      s.arpIndex++;
      break;
    }
    case FxArpMode::Random:
      position = random() % length;
      break;
    default: // up, as played
      position = s.arpIndex % length;
      s.arpIndex++;
      break;
  }
  const ArpNote &a = sorted[position % s.arpCount];
  uint8_t note = clampNote(a.note + 12 * (position / s.arpCount));
  emitNote(input, 0x90, a.channel, note, a.velocity);

  // Gate: the note-off after a share of the arp step (always before the next arp note)
  uint32_t gate = (uint32_t)((uint64_t)lengthUs * c.arpGate / 100);
  if (gate + 1000 > lengthUs) gate = lengthUs > 2000 ? lengthUs - 1000 : lengthUs / 2;
  if (gate < kMinGateMicros) gate = kMinGateMicros;
  for (PendingOff &off : s.offs) {
    if (!off.used) {
      off.used = true;
      off.channel = a.channel;
      off.note = note;
      off.dueUs = timeUs + gate;
      return;
    }
  }
  emitNote(input, 0x80, a.channel, note, 0); // no room to remember it: end it now
}

void FxEngine::arpStep(int input, uint64_t timeUs)
{
  const FxChainConfig &c = _active[input];
  ChainState &s = _state[input];
  if (!c.on(FxSlot::Arp) || s.arpCount == 0) return;
  static const int kStepsPerNote[(int)FxArpRate::Count] = {4, 2, 1, 1};
  int every = kStepsPerNote[(int)c.arpRate];
  if (s.stepCount++ % every != 0) return;
  if (c.arpRate == FxArpRate::R32) {
    arpPlay(input, timeUs, _stepUs / 2);
    s.secondDue = true; // the offbeat 32nd comes from run()
    s.secondUs = timeUs + _stepUs / 2;
  } else {
    arpPlay(input, timeUs, (uint32_t)every * _stepUs);
  }
}

void FxEngine::arpReleaseSounding(int input)
{
  ChainState &s = _state[input];
  for (PendingOff &off : s.offs) {
    if (off.used) {
      emitNote(input, 0x80, off.channel, off.note, 0);
      off.used = false;
    }
  }
  s.secondDue = false;
}

void FxEngine::arpClear(int input)
{
  ChainState &s = _state[input];
  s.arpCount = 0;
  s.arpIndex = 0;
  s.stepCount = 0;
}

void FxEngine::reset(MidiSource input)
{
  _state[(int)input] = ChainState();
  _held[(int)input] = 0;
}

void FxEngine::onClock(const ClockEvent &event, void *ctx)
{
  FxEngine *fx = static_cast<FxEngine *>(ctx);
  switch (event.type) {
    case ClockEvent::Start:
    case ClockEvent::Continue:
      fx->_running = true;
      for (ChainState &s : fx->_state) {
        s.arpIndex = 0;
        s.stepCount = 0;
      }
      break;
    case ClockEvent::Stop:
      // Stop (also sent by panic) ends the arp and its latched notes
      fx->_running = false;
      for (int input = 0; input < 2; input++) {
        fx->arpReleaseSounding(input);
        fx->arpClear(input);
      }
      break;
    case ClockEvent::Step: {
      if (fx->_lastStepUs) {
        uint64_t interval = event.timeUs - fx->_lastStepUs;
        if (interval > 20000 && interval < 2000000) fx->_stepUs = (uint32_t)interval;
      }
      fx->_lastStepUs = event.timeUs;
      fx->_running = true;
      for (int input = 0; input < 2; input++) fx->arpStep(input, event.timeUs);
      break;
    }
    default:
      break;
  }
}

void FxEngine::run()
{
  applyPending();
  uint64_t now = nowMicros64();
  for (int input = 0; input < 2; input++) {
    const FxChainConfig &c = _active[input];
    ChainState &s = _state[input];
    for (PendingOff &off : s.offs) {
      if (off.used && now >= off.dueUs) {
        emitNote(input, 0x80, off.channel, off.note, 0);
        off.used = false;
      }
    }
    if (s.secondDue && now >= s.secondUs) {
      s.secondDue = false;
      if (c.on(FxSlot::Arp) && _running) arpPlay(input, s.secondUs, _stepUs / 2);
    }
    // CC smoothing: move towards the target, covering the full range in ccSmooth x 10 ms
    if (c.on(FxSlot::Cc) && c.ccSmooth && s.ccTarget >= 0 && s.ccValue >= 0 && s.ccValue != s.ccTarget &&
        now >= s.ccNextUs) {
      int step = (127 + c.ccSmooth - 1) / c.ccSmooth;
      int delta = s.ccTarget - s.ccValue;
      s.ccValue += delta > 0 ? (delta < step ? delta : step) : (-delta < step ? delta : -step);
      uint8_t bytes[3] = {(uint8_t)(0xB0 | s.ccChannel), c.ccTarget, (uint8_t)s.ccValue};
      if (_emit) _emit((MidiSource)input, {(MidiSource)input, 0, bytes, 3}, _emitCtx);
      s.ccNextUs = now + kSmoothMicros;
    }
    _held[input] = s.mapped;
  }
}
