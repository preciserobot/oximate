#include "gen_page.h"

#include "drum_kits.h"
#include "euclid.h"

using namespace Layout;

// List view
static constexpr int kRowY = 24, kRowH = 24;
static constexpr int kKindX = 18, kKindW = 48, kChanX = 70, kPreviewX = 100, kMuteX = 268, kMuteW = 52;
// Edit view
static constexpr int kBackY = 24, kBackH = 22;
static constexpr int kFieldY = 50, kFieldH = 20, kFieldW = 160;
static constexpr int kInfoY = 194;

static constexpr uint32_t kRepeatDelayMillis = 400, kRepeatMillis = 60, kRedrawMillis = 125;

static const char *const kKindNames[] = {"off", "EUC", "TUR", "MOD"};
static const char *const kRateNames[] = {"1/32", "1/16", "1/8", "1/4", "1/16T"};
static const char *const kShapeNames[] = {"S&H", "Smooth", "Sine", "Tri", "Ramp+", "Ramp-", "Square"};
static const char *const kOutNames[] = {"-", "DIN", "USB", "Both"};

static const GenPage::Field kEuclidFields[] = {GenPage::Out,   GenPage::Chan, GenPage::Rate,    GenPage::Prob,
                                               GenPage::Steps, GenPage::Hits, GenPage::Rot,     GenPage::Note,
                                               GenPage::Vel,   GenPage::Gate, GenPage::AccHits, GenPage::AccRot};
static const GenPage::Field kTuringFields[] = {GenPage::Out,   GenPage::Chan,   GenPage::Rate,  GenPage::Prob,
                                               GenPage::Len,   GenPage::Mutate, GenPage::Scale, GenPage::Root,
                                               GenPage::Oct,   GenPage::Range,  GenPage::Density, GenPage::Vel,
                                               GenPage::Gate};
static const GenPage::Field kModFields[] = {GenPage::Out,    GenPage::Chan, GenPage::Cc, GenPage::Shape,
                                            GenPage::Period, GenPage::Min,  GenPage::Max};

static int clampValue(int value, int low, int high) { return value < low ? low : (value > high ? high : value); }

int GenPage::fields(GenKind kind, const Field **out)
{
  switch (kind) {
    case GenKind::Euclid: *out = kEuclidFields; return sizeof(kEuclidFields) / sizeof(Field);
    case GenKind::Turing: *out = kTuringFields; return sizeof(kTuringFields) / sizeof(Field);
    case GenKind::Mod:    *out = kModFields; return sizeof(kModFields) / sizeof(Field);
    default:              *out = nullptr; return 0;
  }
}

const char *GenPage::fieldName(Field field)
{
  static const char *const kNames[] = {"Out",   "Chan", "Rate", "Prob",  "Gate",  "Vel",     "Steps", "Hits",
                                       "Rot",   "Note", "AccH", "AccR",  "Len",   "Mutate",  "Scale", "Root",
                                       "Oct",   "Range", "Dens", "CC",   "Shape", "Period",  "Min",   "Max"};
  return kNames[field];
}

void GenPage::fieldText(const GenSlot &s, Field field, char *out, size_t size)
{
  switch (field) {
    case Out:     snprintf(out, size, "%s", kOutNames[s.outputs & 3]); break;
    case Chan:    snprintf(out, size, "%u", s.channel); break;
    case Rate:    snprintf(out, size, "%s", kRateNames[(int)s.rate]); break;
    case Prob:    snprintf(out, size, "%u%%", s.probability); break;
    case Gate:    snprintf(out, size, "%u%%", s.gate); break;
    case Vel:     snprintf(out, size, "%u", s.velocity); break;
    case Steps:   snprintf(out, size, "%u", s.steps); break;
    case Hits:    snprintf(out, size, "%u", s.hits); break;
    case Rot:     snprintf(out, size, "%u", s.rotation); break;
    case Note:
      if (s.channel == 10) snprintf(out, size, "%u %s", s.note, DrumKits::noteName(s.note));
      else MusicTheory::noteName(s.note, out, size);
      break;
    case AccHits: snprintf(out, size, "%u", s.accentHits); break;
    case AccRot:  snprintf(out, size, "%u", s.accentRotation); break;
    case Len:     snprintf(out, size, "%u", s.length); break;
    case Mutate:  snprintf(out, size, "%u%%", s.mutate); break;
    case Scale:   snprintf(out, size, "%s", MusicTheory::kScales[s.scale % MusicTheory::kScaleCount].name); break;
    case Root:    snprintf(out, size, "%s", MusicTheory::pitchClassName(s.root % 12)); break;
    case Oct:     snprintf(out, size, "C%u", s.octave); break;
    case Range:   snprintf(out, size, "%u oct", s.range); break;
    case Density: snprintf(out, size, "%u%%", s.density); break;
    case Cc:      snprintf(out, size, "%u", s.cc); break;
    case Shape:   snprintf(out, size, "%s", kShapeNames[(int)s.shape]); break;
    case Period:
      if (s.period % 16 == 0) snprintf(out, size, "%u bar", s.period / 16);
      else snprintf(out, size, "%u st", s.period);
      break;
    case Min:     snprintf(out, size, "%u", s.min); break;
    default:      snprintf(out, size, "%u", s.max); break;
  }
}

void GenPage::adjustField(GenSlot &s, Field field, int d)
{
  switch (field) {
    case Out:     s.outputs = clampValue(s.outputs + d, 1, 3); break;
    case Chan:    s.channel = clampValue(s.channel + d, 1, 16); break;
    case Rate:    s.rate = (GenRate)(((int)s.rate + (int)GenRate::Count + d) % (int)GenRate::Count); break;
    case Prob:    s.probability = clampValue(s.probability + d * 5, 0, 100); break;
    case Gate:    s.gate = clampValue(s.gate + d * 5, 5, 100); break;
    case Vel:     s.velocity = clampValue(s.velocity + d, 1, 127); break;
    case Steps:
      s.steps = clampValue(s.steps + d, 1, 32);
      s.hits = clampValue(s.hits, 0, s.steps);
      break;
    case Hits:    s.hits = clampValue(s.hits + d, 0, s.steps); break;
    case Rot:     s.rotation = (s.rotation + s.steps + d) % s.steps; break;
    case Note:    s.note = clampValue(s.note + d, 0, 127); break;
    case AccHits: s.accentHits = clampValue(s.accentHits + d, 0, s.steps); break;
    case AccRot:  s.accentRotation = (s.accentRotation + s.steps + d) % s.steps; break;
    case Len:     s.length = clampValue(s.length + d, 2, TuringRegister::kMaxLength); break;
    case Mutate:  s.mutate = clampValue(s.mutate + d * 5, 0, 100); break;
    case Scale:   s.scale = (s.scale + MusicTheory::kScaleCount + d) % MusicTheory::kScaleCount; break;
    case Root:    s.root = (s.root + 12 + d) % 12; break;
    case Oct:     s.octave = clampValue(s.octave + d, 1, 6); break;
    case Range:   s.range = clampValue(s.range + d, 1, 3); break;
    case Density: s.density = clampValue(s.density + d * 5, 0, 100); break;
    case Cc:      s.cc = clampValue(s.cc + d, 0, 119); break;
    case Shape:   s.shape = (ModShape)(((int)s.shape + (int)ModShape::Count + d) % (int)ModShape::Count); break;
    case Period:  {
      // 1-16 steps one at a time, then whole bars
      int period = s.period;
      period = period < 16 || (period == 16 && d < 0) ? period + d : period + d * 16;
      s.period = clampValue(period, 1, 1024);
      break;
    }
    case Min:     s.min = clampValue(s.min + d, 0, 127); break;
    default:      s.max = clampValue(s.max + d, 0, 127); break;
  }
}

// ---------------------------------------------------------------------------- buttons and touch

void GenPage::buttonLabels(const char *labels[3]) const
{
  const GenSlot &s = _gen.config().slots[_slot];
  const char *live = s.kind == GenKind::Turing ? (s.locked ? "Unlock" : "Lock") : (s.muted ? "Unmute" : "Mute");
  if (s.kind == GenKind::Off) live = "";
  labels[0] = _editing ? "-" : "Prev";
  labels[1] = live;
  labels[2] = _editing ? "+" : "Next";
}

void GenPage::changed()
{
  _gen.changed();
  markDirty();
  markFooterDirty();
}

/*! B: mute (Euclid, Mod) or lock (Turing) */
void GenPage::liveAction()
{
  GenSlot &s = _gen.config().slots[_slot];
  if (s.kind == GenKind::Turing) s.locked = !s.locked;
  else if (s.kind != GenKind::Off) s.muted = !s.muted;
  changed();
}

void GenPage::onButton(int index)
{
  if (index == 1) {
    liveAction();
    return;
  }
  int direction = index == 0 ? -1 : +1;
  if (_editing) {
    const Field *list;
    int count = fields(_gen.config().slots[_slot].kind, &list);
    if (count) {
      adjustField(_gen.config().slots[_slot], list[_field % count], direction);
      changed();
    }
  } else {
    _slot = (_slot + GenEngine::kSlots + direction) % GenEngine::kSlots;
    markDirty();
    markFooterDirty();
  }
}

void GenPage::onButtonHold(int index)
{
  GenSlot &s = _gen.config().slots[_slot];
  if (index == 0 && !_editing) {
    _gen.realign(); // every slot back to step 1 on the next bar
  } else if (index == 1) {
    if (s.kind == GenKind::Turing) {
      s.seed = s.seed * 1664525u + 1013904223u + millis(); // reseed: a new register
      changed();
    } else if (s.kind == GenKind::Euclid) {
      s.rotation = (s.rotation + 1 + millis() % (s.steps ? s.steps : 1)) % (s.steps ? s.steps : 1);
      changed();
    }
  }
}

void GenPage::tick()
{
  renderLeds();
  // Holding A or C repeats - / + in the edit view
  if (_editing) {
    m5::Button_Class *buttons[2] = {&M5.BtnA, &M5.BtnC};
    for (int i = 0; i < 2; i++) {
      if (buttons[i]->pressedFor(kRepeatDelayMillis) && millis() - _repeatAt >= kRepeatMillis) {
        _repeatAt = millis();
        onButton(i == 0 ? 0 : 2);
      }
    }
  }
  if (millis() - _lastDraw >= kRedrawMillis && _gen.playing()) {
    _lastDraw = millis();
    markDirty(); // playheads move
  }
}

void GenPage::onTouch(int x, int y)
{
  GenConfig &c = _gen.config();
  if (_editing) {
    if (y >= kBackY && y < kBackY + kBackH && x < 80) {
      _editing = false;
    } else if (y >= kFieldY) {
      const Field *list;
      int count = fields(c.slots[_slot].kind, &list);
      int index = ((y - kFieldY) / kFieldH) * 2 + (x >= kFieldW ? 1 : 0);
      if (index >= count) return;
      _field = index;
    } else {
      return;
    }
    markDirty();
    markFooterDirty();
    return;
  }
  int row = (y - kRowY) / kRowH;
  if (y < kRowY || row >= GenEngine::kSlots) return;
  _slot = row;
  GenSlot &s = c.slots[row];
  if (x >= kKindX && x < kKindX + kKindW) {
    s.kind = (GenKind)(((int)s.kind + 1) % (int)GenKind::Count);
    if (s.kind == GenKind::Mod) s.channel = s.channel == 10 ? 3 : s.channel;
    changed();
    return;
  }
  if (x >= kMuteX && s.kind != GenKind::Off) {
    s.muted = !s.muted;
    changed();
    return;
  }
  if (x >= kPreviewX && s.kind != GenKind::Off) {
    _editing = true;
    _field = 0;
  }
  markDirty();
  markFooterDirty();
}

void GenPage::enter()
{
  Page::enter();
  _leds.claim(kLedOwner, 0, Leds::kCount);
}

void GenPage::leave()
{
  _editing = false;
  _leds.release(kLedOwner);
}

// ---------------------------------------------------------------------------- drawing

/*! One-line live view of a slot: Euclid steps with the playhead, Turing's coming notes, a mod value */
void GenPage::preview(int slot, char *out, size_t size) const
{
  const GenSlot &s = _gen.config().slots[slot];
  GenSlotStatus status = _gen.status(slot);
  out[0] = '\0';
  if (s.kind == GenKind::Euclid) {
    int steps = s.steps ? s.steps : 1, shown = steps < 20 ? steps : 20;
    uint32_t mask = euclid(steps, s.hits, s.rotation);
    int playhead = status.position ? (status.position - 1) % steps : -1;
    size_t n = 0;
    for (int i = 0; i < shown && n + 1 < size; i++) out[n++] = i == playhead ? '#' : ((mask >> i) & 1 ? 'x' : '.');
    out[n] = '\0';
  } else if (s.kind == GenKind::Turing) {
    size_t n = 0;
    for (int i = 0; i < 4 && n + 6 < size; i++) {
      int index = (status.registerPosition + i) % (s.length ? s.length : 1);
      int note = turingNote(status.registerValues[index], s.density, s.root % 12,
                            MusicTheory::kScales[s.scale % MusicTheory::kScaleCount].mask, s.octave, s.range);
      char name[6] = ".";
      if (note >= 0) MusicTheory::noteName(note, name, sizeof(name));
      n += snprintf(out + n, size - n, "%-5s", name);
    }
  } else if (s.kind == GenKind::Mod) {
    snprintf(out, size, "CC%u %3u", s.cc, status.modValue);
  }
}

void GenPage::draw()
{
  M5Canvas &c = canvas();
  c.fillRect(0, kContentTop, c.width(), kContentHeight, TFT_BLACK);
  if (_editing) {
    drawEdit();
  } else {
    drawList();
  }
}

void GenPage::drawList()
{
  M5Canvas &c = canvas();
  char text[40];
  for (int slot = 0; slot < GenEngine::kSlots; slot++) {
    const GenSlot &s = _gen.config().slots[slot];
    int y = kRowY + slot * kRowH;
    if (slot == _slot) c.fillRect(0, y, c.width(), kRowH, 0x18E3);
    uint16_t background = slot == _slot ? 0x18E3 : TFT_BLACK;
    snprintf(text, sizeof(text), "%d", slot + 1);
    c.setTextColor(TFT_LIGHTGREY, background);
    c.drawString(text, 4, y + 4);
    drawChip(c, kKindX, y, kKindW, kRowH, kKindNames[(int)s.kind], s.kind != GenKind::Off);
    if (s.kind == GenKind::Off) continue;
    snprintf(text, sizeof(text), "%2u", s.channel);
    c.setTextColor(TFT_WHITE, background);
    c.drawString(text, kChanX, y + 4);
    preview(slot, text, sizeof(text));
    c.setTextColor(s.muted ? TFT_DARKGREY : TFT_CYAN, background);
    c.drawString(text, kPreviewX, y + 4);
    const char *mute = s.kind == GenKind::Turing && s.locked ? (s.muted ? "M L" : "L") : (s.muted ? "M" : "on");
    drawChip(c, kMuteX, y, kMuteW, kRowH, mute, !s.muted, s.muted ? kOnColor : TFT_DARKGREEN);
  }
}

void GenPage::drawEdit()
{
  M5Canvas &c = canvas();
  const GenSlot &s = _gen.config().slots[_slot];
  char text[40], value[24];
  drawChip(c, 0, kBackY, 80, kBackH, "< Back", true);
  snprintf(text, sizeof(text), "Slot %d %s%s", _slot + 1, kKindNames[(int)s.kind], s.muted ? " (muted)" : "");
  c.setTextColor(TFT_WHITE, TFT_BLACK);
  c.drawString(text, 90, kBackY + 3);

  const Field *list;
  int count = fields(s.kind, &list);
  for (int i = 0; i < count; i++) {
    int x = (i % 2) * kFieldW, y = kFieldY + (i / 2) * kFieldH;
    fieldText(s, list[i], value, sizeof(value));
    snprintf(text, sizeof(text), "%-6s %s", fieldName(list[i]), value);
    bool selected = i == _field;
    if (selected) c.fillRect(x, y, kFieldW - 2, kFieldH, TFT_WHITE);
    c.setTextColor(selected ? TFT_BLACK : TFT_WHITE, selected ? TFT_WHITE : TFT_BLACK);
    c.drawString(text, x + 2, y + 2);
  }
  preview(_slot, text, sizeof(text));
  c.setTextColor(TFT_CYAN, TFT_BLACK);
  c.drawString(text, 2, kInfoY + 8);
}

void GenPage::renderLeds()
{
  const GenSlot &s = _gen.config().slots[_slot];
  GenSlotStatus status = _gen.status(_slot);
  const RGBColor off(0, 0, 0);
  for (int i = 0; i < Leds::kCount; i++) _leds.setPixel(kLedOwner, i, off);
  if (s.kind == GenKind::Euclid) {
    // The 10 LEDs as the ring: every step when there are 10 or fewer, else the 10 around the playhead
    int steps = s.steps ? s.steps : 1;
    uint32_t mask = euclid(steps, s.hits, s.rotation);
    int playhead = status.position ? (status.position - 1) % steps : 0;
    int first = steps <= Leds::kCount ? 0 : playhead - Leds::kCount / 2;
    for (int i = 0; i < Leds::kCount && i < steps; i++) {
      int step = ((first + i) % steps + steps) % steps;
      RGBColor color = (mask >> step) & 1 ? RGBColor(0, 160, 255) : RGBColor(0, 12, 24);
      if (step == playhead && _gen.playing()) color = RGBColor(255, 255, 255);
      _leds.setPixel(kLedOwner, i, color);
    }
  } else if (s.kind == GenKind::Turing) {
    if (status.lastNote >= 0) _leds.setPixel(kLedOwner, 0, noteColor(status.lastNote));
    _leds.setPixel(kLedOwner, 9, s.locked ? RGBColor(0, 255, 0) : RGBColor(255, 128, 0));
  } else if (s.kind == GenKind::Mod) {
    int lit = (status.modValue * Leds::kCount + 63) / 127;
    for (int i = 0; i < lit; i++) _leds.setPixel(kLedOwner, i, RGBColor(160, 0, 255));
  }
}
