#include "chord_page.h"

#include <Preferences.h>

using namespace Layout;
using namespace MusicTheory;

static constexpr uint32_t kSaveDelayMillis = 2000;
static const char *const kNamespace = "oximate";
static const char *const kKey = "chord";

// Full view rows
static constexpr int kNameY = 30;
static constexpr int kInversionY = 74;
static constexpr int kNotesY = 94;
static constexpr int kAlsoY = 112;
static constexpr int kRuleY = 132;
static constexpr int kHistoryY = 138;
static constexpr int kKeyY = 158;
static constexpr int kChipsY = 192, kChipH = 24, kChipW = 80;

static const uint16_t kArpMillis[] = {0, 250, 500, 1000, 0};
static const char *const kArpNames[] = {"Arp off", "Arp 250", "Arp 500", "Arp 1s", "Arp bar"};
static const char *const kInputNames[] = {"", "USB", "DIN", "USB+DIN"};
static const char *const kIntervalLong[12] = {"unison", "m2", "M2", "m3", "M3", "P4", "tritone",
                                              "P5", "m6", "M6", "m7", "M7"};

void ChordPage::begin()
{
  load();
  applyFilter();
}

void ChordPage::load()
{
  Preferences prefs;
  if (!prefs.begin(kNamespace, true)) return;
  ChordConfig stored;
  if (prefs.getBytesLength(kKey) == sizeof(stored) && prefs.getBytes(kKey, &stored, sizeof(stored)) == sizeof(stored) &&
      stored.version == ChordConfig::kVersion) {
    _config = stored;
  }
  prefs.end();
}

void ChordPage::save()
{
  Preferences prefs;
  if (prefs.begin(kNamespace, false)) {
    prefs.putBytes(kKey, &_config, sizeof(_config));
    prefs.end();
  }
}

/*! The arpeggio window in ms; "one bar" follows the clock's tempo */
uint32_t ChordPage::arpMillis() const
{
  if (_config.arp != 4) return kArpMillis[_config.arp % 5];
  ClockEngine::Snapshot clock = _clock.snapshot();
  float bpm = clock.bpm > 20 ? clock.bpm : 120;
  return (uint32_t)(60000.0f / bpm * (clock.beatsPerBar ? clock.beatsPerBar : 4));
}

void ChordPage::applyFilter()
{
  ChordFilter filter;
  filter.inputs = _config.inputs;
  filter.channel = _config.channel;
  filter.skipDrums = _config.skipDrums;
  filter.sustain = _config.sustain;
  filter.arpMillis = _appliedArp = arpMillis();
  _input.setFilter(filter);
}

void ChordPage::changed()
{
  applyFilter();
  _dirty = true;
  _changedMillis = millis();
  markDirty();
}

void ChordPage::poll()
{
  if (_dirty && millis() - _changedMillis >= kSaveDelayMillis) {
    save();
    _dirty = false;
  }
  // "Arp bar" follows tempo changes
  static uint32_t lastCheck = 0;
  if (_config.arp == 4 && millis() - lastCheck >= 1000) {
    lastCheck = millis();
    uint32_t window = arpMillis();
    if (window > _appliedArp + 20 || window + 20 < _appliedArp) applyFilter();
  }
}

void ChordPage::buttonLabels(const char *labels[3]) const
{
  labels[0] = _detector.frozen() ? "Clear(frz)" : "Clear";
  labels[1] = _config.simpleView ? "View:big" : "View:full";
  labels[2] = _config.flats ? "Notes:b" : "Notes:#";
}

void ChordPage::onButton(int index)
{
  switch (index) {
    case 0: // clear history and held notes (a manual unstick)
      _detector.clear();
      _input.requestClear();
      break;
    case 1:
      _config.simpleView = !_config.simpleView;
      changed();
      break;
    case 2:
      _config.flats = !_config.flats;
      changed();
      break;
  }
  markDirty();
}

void ChordPage::onButtonHold(int index)
{
  if (index == 0) {
    _detector.setFrozen(!_detector.frozen()); // freeze the last chord to read it after it ends
    markDirty();
  }
}

void ChordPage::onTouch(int x, int y)
{
  if (_config.simpleView || y < kChipsY || y >= kChipsY + kChipH) return;
  switch (x / kChipW) {
    case 0: // input: USB+DIN -> USB -> DIN
      _config.inputs = _config.inputs == 3 ? 1 : (_config.inputs == 1 ? 2 : 3);
      break;
    case 1: // channel: all but drums -> all -> 1..16
      if (_config.channel == 0 && _config.skipDrums) {
        _config.skipDrums = false;
      } else if (_config.channel < 16) {
        _config.channel++;
      } else {
        _config.channel = 0;
        _config.skipDrums = true;
      }
      break;
    case 2:
      _config.arp = (_config.arp + 1) % 5;
      break;
    default:
      _config.sustain = !_config.sustain;
      break;
  }
  changed();
}

void ChordPage::enter()
{
  Page::enter();
  _visible = true;
  _leds.claim(kOwner, 0, Leds::kCount);
}

void ChordPage::leave()
{
  _visible = false;
  _leds.release(kOwner);
}

void ChordPage::tick()
{
  if (_detector.version() != _shownVersion) {
    _shownVersion = _detector.version();
    markDirty();
    markFooterDirty(); // the freeze marker on A
  }
  renderLeds();
}

void ChordPage::draw()
{
  canvas().fillRect(0, kContentTop, canvas().width(), kContentHeight, TFT_BLACK);
  if (_config.simpleView) {
    drawSimple();
  } else {
    drawFull();
  }
}

/*! The big chord name, centred; `large` doubles the font when it still fits */
void ChordPage::drawChordName(const char *name, int y, uint16_t color, bool large)
{
  M5Canvas &c = canvas();
  c.setFont(&fonts::FreeSansBold24pt7b);
  c.setTextSize(large && c.textWidth(name) * 2 <= c.width() - 8 ? 2 : 1);
  c.setTextColor(color);
  c.setTextDatum(top_center);
  c.drawString(name, c.width() / 2, y);
  c.setTextDatum(top_left);
  c.setTextSize(1);
  c.setFont(&fonts::AsciiFont8x16);
}

static void currentText(const ChordDetector::Result &r, bool flats, char *out, size_t size)
{
  switch (r.kind) {
    case ChordDetector::Kind::Empty:
      snprintf(out, size, "-");
      break;
    case ChordDetector::Kind::Single:
      snprintf(out, size, "%s", pitchClassName(r.bass % 12, flats));
      break;
    case ChordDetector::Kind::Interval:
      snprintf(out, size, "%s+%s", pitchClassName(r.bass % 12, flats),
               pitchClassName((r.bass + r.interval) % 12, flats));
      break;
    case ChordDetector::Kind::Chord:
      leadSheetName(r.best, out, size, flats);
      break;
    default:
      snprintf(out, size, "?");
      break;
  }
}

static uint16_t nameColor(const ChordDetector &detector)
{
  if (detector.frozen()) return TFT_CYAN;
  if (detector.settling()) return TFT_DARKGREY;
  return detector.result().kind == ChordDetector::Kind::Unknown ? TFT_ORANGE : TFT_WHITE;
}

void ChordPage::drawSimple()
{
  char name[24];
  currentText(_detector.result(), _config.flats, name, sizeof(name));
  drawChordName(name, kContentTop + 60, nameColor(_detector), true);
}

void ChordPage::drawFull()
{
  M5Canvas &c = canvas();
  const ChordDetector::Result &r = _detector.result();
  char text[64], name[24];

  currentText(r, _config.flats, name, sizeof(name));
  drawChordName(name, kNameY, nameColor(_detector), false);

  // Inversion, or the interval for two notes
  c.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  c.setTextDatum(top_center);
  if (r.kind == ChordDetector::Kind::Chord && r.best.inversion > 0) {
    snprintf(text, sizeof(text), "%s inversion", inversionName(r.best.inversion));
    c.drawString(text, c.width() / 2, kInversionY);
  } else if (r.kind == ChordDetector::Kind::Interval) {
    c.drawString(kIntervalLong[r.interval % 12], c.width() / 2, kInversionY);
  }
  c.setTextDatum(top_left);

  // Notes, lowest first, in their pitch-class colours (the same as the LEDs)
  c.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  c.setCursor(4, kNotesY);
  c.print("Notes ");
  for (int i = 0; i < r.noteCount && c.getCursorX() < c.width() - 40; i++) {
    RGBColor color = noteColor(r.notes[i]);
    noteName(r.notes[i], text, sizeof(text), _config.flats);
    c.setTextColor(c.color565(color.R8(), color.G8(), color.B8()), TFT_BLACK);
    c.print(text);
    c.print(" ");
  }

  // Alternatives
  if (r.kind == ChordDetector::Kind::Chord && r.alternativeCount) {
    int n = snprintf(text, sizeof(text), "Also  ");
    for (int i = 0; i < r.alternativeCount; i++) {
      leadSheetName(r.alternatives[i], name, sizeof(name), _config.flats);
      n += snprintf(text + n, sizeof(text) - n, "%s  ", name);
    }
    c.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    c.drawString(text, 4, kAlsoY);
  }

  c.drawFastHLine(4, kRuleY, c.width() - 8, TFT_DARKGREY);

  // History: the last 4 distinct chords, newest last
  c.setTextColor(TFT_WHITE, TFT_BLACK);
  c.setCursor(4, kHistoryY);
  for (int i = 0; i < _detector.historyCount(); i++) {
    leadSheetName(_detector.history(i), name, sizeof(name), _config.flats);
    c.print(name);
    c.print("  ");
  }

  // Key estimate from the history
  KeyEstimate key = _detector.key();
  if (key.found && _detector.historyCount() >= 2) {
    snprintf(text, sizeof(text), "Key  %s %s%s", pitchClassName(key.tonic, _config.flats), key.minor ? "minor" : "major",
             key.correlation < 0.6f ? " (?)" : "");
  } else {
    snprintf(text, sizeof(text), "Key  -");
  }
  c.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  c.drawString(text, 4, kKeyY);

  // Settings chips
  char channel[10];
  if (_config.channel) {
    snprintf(channel, sizeof(channel), "ch %u", _config.channel);
  } else {
    snprintf(channel, sizeof(channel), _config.skipDrums ? "ch-10" : "all ch");
  }
  drawChip(c, 0 * kChipW, kChipsY, kChipW, kChipH, kInputNames[_config.inputs & 3], true);
  drawChip(c, 1 * kChipW, kChipsY, kChipW, kChipH, channel, true);
  drawChip(c, 2 * kChipW, kChipsY, kChipW, kChipH, kArpNames[_config.arp % 5], _config.arp != 0);
  drawChip(c, 3 * kChipW, kChipsY, kChipW, kChipH, _config.sustain ? "Sus on" : "Sus off", _config.sustain);
}

void ChordPage::renderLeds()
{
  if (!_visible) return;
  const ChordDetector::Result &r = _detector.result();
  const RGBColor off(0, 0, 0);
  RGBColor colors[Leds::kCount];
  for (RGBColor &c : colors) c = off;
  if (r.kind == ChordDetector::Kind::Chord) {
    // LEDs 0-4: chord tones, root first; 5-8: the tones beyond five
    const ExtendedChord &chord = kExtendedChords[r.best.type];
    for (int i = 0; i < chord.count && i < 9; i++) {
      colors[i] = noteColor((r.best.root + chord.tones[i]) % 12);
    }
    // LED 9: green = clear reading, yellow = alternatives close by
    colors[9] = r.alternativeCount ? RGBColor(255, 200, 0) : RGBColor(0, 255, 0);
  } else if (r.kind != ChordDetector::Kind::Empty) {
    int i = 0;
    for (int pc = 0; pc < 12 && i < 9; pc++) {
      if (r.pcs & (1 << pc)) colors[i++] = noteColor(pc);
    }
    colors[9] = r.kind == ChordDetector::Kind::Unknown ? RGBColor(255, 0, 0) : off;
  }
  for (int i = 0; i < Leds::kCount; i++) _leds.setPixel(kOwner, i, colors[i]);
}
