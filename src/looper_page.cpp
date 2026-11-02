#include "looper_page.h"

#include <esp_heap_caps.h>
#include <stdlib.h>

#include "smf.h"

using namespace Layout;

// Main view rows
static constexpr int kStateY = 24;
static constexpr int kProgressY = 60, kProgressH = 10;
static constexpr int kRowsY = 76, kRowH = 16, kRowX = 40, kCellW = 4;
static constexpr int kChipsY = 144, kChipH = 26;
static constexpr int kActionsY = 172;
static constexpr int kStatusY = 202;
static constexpr int kPhraseChipX = 248;
static constexpr int kChipW5 = 64, kChipW4 = 80;
// Files view
static constexpr int kFileY = 24, kFileH = 20;

static constexpr uint32_t kRedrawMillis = 50;
static const char *const kFolder = "/oximate/loops";
static const uint8_t kBars[] = {1, 2, 4, 8, 16};
static const char *const kQuantise[] = {"Q off", "Q 1/16", "Q 1/8", "Q 1/16T"};

static uint8_t nextIn(const uint8_t *values, int count, uint8_t current)
{
  for (int i = 0; i < count; i++) {
    if (values[i] == current) return values[(i + 1) % count];
  }
  return values[0];
}

static uint8_t *allocate(size_t bytes)
{
  return (uint8_t *)heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}

void LooperPage::buttonLabels(const char *labels[3]) const
{
  if (_filesView) {
    labels[0] = "Back";
    labels[1] = "";
    labels[2] = "";
    return;
  }
  LoopSnapshot snap = _looper.snapshot();
  switch (snap.state) {
    case LoopState::Empty: labels[0] = "Rec"; break;
    case LoopState::Armed: labels[0] = "Disarm"; break;
    case LoopState::Recording: labels[0] = "Cancel"; break;
    default: labels[0] = "Hold:Undo"; break;
  }
  labels[1] = snap.playing ? "Mute" : "Play";
  labels[2] = snap.state == LoopState::Looping ? (snap.overdub ? "Dub off" : "Overdub") : "";
}

void LooperPage::onButton(int index)
{
  if (_filesView) {
    if (index == 0) {
      _filesView = false;
      markDirty();
    }
    return;
  }
  switch (index) {
    case 0: _looper.rec(); break;
    case 1: _looper.togglePlay(); break;
    case 2: _looper.toggleOverdub(); break;
  }
}

void LooperPage::onButtonHold(int index)
{
  if (!_filesView && index == 0) {
    _looper.undo();
    status("Undo: last pass removed");
  }
}

void LooperPage::status(const char *text)
{
  strncpy(_status, text, sizeof(_status) - 1);
  markDirty();
}

void LooperPage::enter()
{
  Page::enter();
  _filesView = false;
  _leds.claim(kLedOwner, 0, Leds::kCount);
}

void LooperPage::leave() { _leds.release(kLedOwner); }

void LooperPage::onTouch(int x, int y)
{
  if (_filesView) {
    int index = (y - kFileY) / kFileH;
    if (y >= kFileY && index >= 0 && index < _fileCount) startLoad(index);
    return;
  }
  touchMain(x, y);
}

void LooperPage::touchMain(int x, int y)
{
  LoopConfig &c = _looper.config();
  if (y >= kStateY && y < kStateY + kChipH && x >= kPhraseChipX) {
    c.startOnPhrase = !c.startOnPhrase;
  } else if (y >= kChipsY && y < kChipsY + kChipH) {
    switch (x / kChipW5) {
      case 0: c.bars = nextIn(kBars, 5, c.bars); break;
      case 1: c.quantise = (c.quantise + 1) % 4; break;
      case 2: c.inputs = c.inputs == 3 ? 1 : (c.inputs == 1 ? 2 : 3); break;
      case 3: c.channel = (c.channel + 1) % 17; break;
      default: c.outputs = c.outputs == 1 ? 2 : (c.outputs == 2 ? 3 : 1); break;
    }
  } else if (y >= kActionsY && y < kActionsY + kChipH) {
    switch (x / kChipW4) {
      case 0: _looper.undo(); status("Undo: last pass removed"); return;
      case 1: _looper.clear(); status("Cleared"); return;
      case 2: startSave(); return;
      default: openFiles(); return;
    }
  } else {
    return;
  }
  _looper.changed();
  markDirty();
}

// ---------------------------------------------------------------------------- files

void LooperPage::freeFile()
{
  if (_file) heap_caps_free(_file);
  _file = nullptr;
}

void LooperPage::saved(bool ok, void *ctx) { static_cast<LooperPage *>(ctx)->_result = ok ? 1 : -1; }

void LooperPage::loaded(bool ok, size_t length, void *ctx)
{
  LooperPage *page = static_cast<LooperPage *>(ctx);
  page->_readLength = length;
  page->_result = ok ? 1 : -1;
}

void LooperPage::startSave()
{
  if (!SdStore::mounted()) return status("No SD card (FAT32 needed)");
  if (_step != FileStep::Idle || SdStore::busy()) return status("Busy, try again");
  if (!_looper.snapshot().events) return status("Nothing to save");
  _looper.requestExport();
  _step = FileStep::Exporting;
  status("Saving...");
}

/*! After the looper's copy: encode it as a MIDI file and hand it to the SD task */
void LooperPage::continueSave()
{
  uint32_t count = _looper.transferCount();
  const LoopEvent *events = _looper.transfer();
  ClockEngine::Snapshot clock = _clock.snapshot();
  Smf::Info info;
  info.bpm = clock.bpm > 0 ? clock.bpm : 120;
  info.beatsPerBar = clock.beatsPerBar ? clock.beatsPerBar : 4;
  info.lengthTicks = _looper.transferLength();
  bool channels[16] = {};
  int used = 0;
  for (uint32_t i = 0; i < count; i++) {
    if (!channels[events[i].status & 0x0F]) used++;
    channels[events[i].status & 0x0F] = true;
  }
  bool type1 = used > 1; // one track per channel
  Smf::Sink size;
  size_t bytes = Smf::write(events, count, info, type1, size);
  freeFile();
  _file = allocate(bytes);
  if (!_file) {
    _step = FileStep::Idle;
    return status("Out of memory");
  }
  Smf::Sink sink;
  sink.data = _file;
  sink.capacity = bytes;
  Smf::write(events, count, info, type1, sink);

  // Next free number: the list is newest first
  char names[1][SdStore::kNameLength];
  int number = 1;
  if (SdStore::list(kFolder, ".mid", names, 1) == 1) number = atoi(names[0] + 4) + 1;
  snprintf(_path, sizeof(_path), "%s/loop%03d.mid", kFolder, number);
  _result = 0;
  if (!SdStore::writeFile(_path, _file, bytes, saved, this)) {
    freeFile();
    _step = FileStep::Idle;
    return status("SD busy");
  }
  _step = FileStep::Writing;
}

void LooperPage::openFiles()
{
  if (!SdStore::mounted()) return status("No SD card (FAT32 needed)");
  _fileCount = SdStore::list(kFolder, ".mid", _names, kFiles);
  _filesView = true;
  markDirty();
}

void LooperPage::startLoad(int index)
{
  if (_step != FileStep::Idle || SdStore::busy()) return status("Busy, try again");
  freeFile();
  _file = allocate(kLoadCapacity);
  if (!_file) return status("Out of memory");
  snprintf(_path, sizeof(_path), "%s/%s", kFolder, _names[index]);
  _result = 0;
  if (!SdStore::readFile(_path, _file, kLoadCapacity, loaded, this)) {
    freeFile();
    return status("SD busy");
  }
  _step = FileStep::Reading;
  _filesView = false;
  status("Loading...");
}

void LooperPage::finishLoad()
{
  uint32_t count = 0;
  Smf::Info info;
  bool ok = Smf::read(_file, _readLength, _looper.transferBuffer(), _looper.capacity(), count, info);
  freeFile();
  if (!ok) return status("Not a MIDI file, or too long");
  // The loop keeps its own length; the clock's tempo is left alone (live!)
  _looper.importTransfer(count, info.lengthTicks);
  char text[48];
  snprintf(text, sizeof(text), "Loaded %s", strrchr(_path, '/') + 1);
  status(text);
}

void LooperPage::tick()
{
  LoopSnapshot snap = _looper.snapshot();
  renderLeds(snap);

  switch (_step) {
    case FileStep::Exporting:
      if (_looper.exportReady()) continueSave();
      break;
    case FileStep::Writing:
      if (_result != 0) {
        freeFile();
        _step = FileStep::Idle;
        char text[48];
        snprintf(text, sizeof(text), _result > 0 ? "Saved %s" : "Save failed: %s", strrchr(_path, '/') + 1);
        status(text);
      }
      break;
    case FileStep::Reading:
      if (_result != 0) {
        _step = FileStep::Idle;
        if (_result > 0) {
          finishLoad();
        } else {
          freeFile();
          status("Load failed");
        }
      }
      break;
    default:
      break;
  }

  if (millis() - _lastDraw >= kRedrawMillis &&
      (snap.position != _shown.position || snap.state != _shown.state || snap.changes != _shown.changes ||
       snap.overdub != _shown.overdub || snap.playing != _shown.playing)) {
    if (snap.state != _shown.state || snap.overdub != _shown.overdub || snap.playing != _shown.playing) {
      markFooterDirty();
    }
    _lastDraw = millis();
    markDirty();
  }
}

// ---------------------------------------------------------------------------- drawing

void LooperPage::draw()
{
  _shown = _looper.snapshot();
  M5Canvas &c = canvas();
  c.fillRect(0, kContentTop, c.width(), kContentHeight, TFT_BLACK);
  if (_filesView) drawFiles();
  else drawMain();
}

void LooperPage::drawMain()
{
  M5Canvas &c = canvas();
  const LoopSnapshot &s = _shown;
  const LoopConfig &cfg = _looper.config();

  const char *state = "EMPTY";
  uint16_t color = TFT_DARKGREY;
  switch (s.state) {
    case LoopState::Armed: state = "ARMED"; color = TFT_RED; break;
    case LoopState::Recording: state = "REC"; color = TFT_RED; break;
    case LoopState::Looping:
      state = s.overdub ? "DUB" : (s.active ? (s.playing ? "LOOP" : "MUTED") : "READY");
      color = s.overdub ? TFT_ORANGE : (s.playing ? TFT_GREEN : TFT_LIGHTGREY);
      break;
    default: break;
  }
  c.setTextSize(2);
  c.setTextColor(color, TFT_BLACK);
  c.setCursor(4, kStateY + 2);
  c.print(state);
  c.setTextSize(1);

  char text[40];
  uint32_t barLength = s.length && s.bars ? s.length / s.bars : 0;
  if (s.length) {
    snprintf(text, sizeof(text), "bar %u/%u", barLength ? s.position / barLength + 1 : 1, s.bars);
  } else {
    snprintf(text, sizeof(text), "%u bars", cfg.bars);
  }
  c.setTextColor(TFT_WHITE, TFT_BLACK);
  c.setCursor(120, kStateY);
  c.print(text);
  snprintf(text, sizeof(text), "pass %u  %u ev", s.passes, s.events);
  c.setCursor(120, kStateY + 16);
  c.print(text);
  drawChip(c, kPhraseChipX, kStateY, 320 - kPhraseChipX, kChipH, cfg.startOnPhrase ? "Phrase" : "Bar",
           cfg.startOnPhrase);

  // Progress through the loop
  c.drawRect(0, kProgressY, 320, kProgressH, TFT_DARKGREY);
  if (s.length) {
    int w = (int)((uint64_t)s.position * 318 / s.length);
    c.fillRect(1, kProgressY + 1, w, kProgressH - 2, color);
  }

  // Overview: the busiest channels, note-ons across the loop
  for (int row = 0; row < 4; row++) {
    int y = kRowsY + row * kRowH;
    if (!s.rowChannel[row]) continue;
    snprintf(text, sizeof(text), "ch%u", s.rowChannel[row]);
    c.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    c.setCursor(0, y);
    c.print(text);
    for (int col = 0; col < 64; col++) {
      if (s.rowBits[row] & (1ull << col)) c.fillRect(kRowX + col * kCellW, y + 4, kCellW - 1, kRowH - 8, TFT_CYAN);
    }
  }
  if (s.length) { // playhead
    int x = kRowX + (int)((uint64_t)s.position * 64 * kCellW / s.length);
    c.drawFastVLine(x, kRowsY, 4 * kRowH, TFT_WHITE);
  }

  char chips[5][12];
  snprintf(chips[0], sizeof(chips[0]), "%u bar%s", cfg.bars, cfg.bars > 1 ? "s" : "");
  snprintf(chips[1], sizeof(chips[1]), "%s", kQuantise[cfg.quantise & 3]);
  snprintf(chips[2], sizeof(chips[2]), "%s", cfg.inputs == 3 ? "In all" : (cfg.inputs == 1 ? "In USB" : "In DIN"));
  if (cfg.channel) snprintf(chips[3], sizeof(chips[3]), "Ch %u", cfg.channel);
  else strcpy(chips[3], "Ch all");
  snprintf(chips[4], sizeof(chips[4]), "%s", cfg.outputs == 1 ? "Out DIN" : (cfg.outputs == 2 ? "Out USB" : "Out D+U"));
  for (int i = 0; i < 5; i++) {
    drawChip(c, i * kChipW5, kChipsY, kChipW5, kChipH, chips[i], i == 1 ? cfg.quantise != 0 : true);
  }
  static const char *const kActions[] = {"Undo", "Clear", "Save", "Load"};
  bool sd = SdStore::mounted();
  for (int i = 0; i < 4; i++) {
    bool on = i < 2 ? s.events > 0 : sd;
    drawChip(c, i * kChipW4, kActionsY, kChipW4, kChipH, kActions[i], on);
  }
  c.setTextColor(TFT_YELLOW, TFT_BLACK);
  c.setCursor(4, kStatusY);
  c.print(_status[0] ? _status : (sd ? "" : "No SD card: save/load off"));
}

void LooperPage::drawFiles()
{
  M5Canvas &c = canvas();
  if (!_fileCount) {
    c.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    c.setCursor(4, kFileY + 4);
    c.print("No loops in /oximate/loops");
    return;
  }
  for (int i = 0; i < _fileCount; i++) {
    c.setTextColor(TFT_WHITE, TFT_BLACK);
    c.setCursor(4, kFileY + i * kFileH + 2);
    c.print(_names[i]);
  }
}

void LooperPage::renderLeds(const LoopSnapshot &s)
{
  uint32_t now = millis();
  int lit = s.length ? (int)((uint64_t)(s.position + 1) * Leds::kCount / s.length) : 0;
  for (int i = 0; i < Leds::kCount; i++) {
    RGBColor color(0, 0, 0);
    switch (s.state) {
      case LoopState::Armed:
        color = (now / 500) % 2 ? RGBColor(160, 0, 0) : RGBColor(20, 0, 0); // slow red pulse
        break;
      case LoopState::Recording:
        color = i < lit ? ((now / 150) % 2 ? RGBColor(255, 0, 0) : RGBColor(120, 0, 0)) : RGBColor(0, 0, 0);
        break;
      case LoopState::Looping:
        if (i < lit && s.active) color = s.overdub ? RGBColor(255, 100, 0) : RGBColor(0, 200, 0);
        break;
      default:
        break;
    }
    if (_step == FileStep::Writing || _step == FileStep::Reading) {
      color = (int)((now / 80) % Leds::kCount) == i ? RGBColor(0, 0, 255) : RGBColor(0, 0, 0); // blue sweep
    }
    _leds.setPixel(kLedOwner, i, color);
  }
}
