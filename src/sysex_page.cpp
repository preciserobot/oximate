#include "sysex_page.h"

#include <Preferences.h>
#include <esp_heap_caps.h>
#include <math.h>

using namespace Layout;

// Library view
static constexpr int kPathY = 24;
static constexpr int kListY = 42, kRowH = 18;
static constexpr int kChipsY = 190, kChipH = 26, kChipW = 80;
// Capture view
static constexpr int kCaptureChipsY = 24;
static constexpr int kLineY = 56;

static constexpr uint32_t kRedrawMillis = 200;
static constexpr uint32_t kSaveDelayMillis = 2000;
static const char *const kFolders[] = {"/syx", "/oximate/dumps"};
static const char *const kDumpFolder = "/oximate/dumps";
static const char *const kOutputNames[] = {"", "Out:DIN", "Out:USB", "Out:Both"};
static const char *const kOutputLong[] = {"", "DIN OUT", "the USB device", "DIN OUT and USB"};

constexpr uint16_t SysExPage::kGaps[];

static uint8_t *allocate(size_t bytes)
{
  return (uint8_t *)heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}

// ---------------------------------------------------------------------------- setup and settings

void SysExPage::begin()
{
  load();
  _sendBuffer = allocate(kSendCapacity);
  _captureBuffer = allocate(kCaptureCapacity);
  _messages = (Syx::Message *)allocate(kMaxMessages * sizeof(Syx::Message));
  _capture.begin(_captureBuffer, _captureBuffer ? kCaptureCapacity : 0);
}

const char *SysExPage::folder() const { return kFolders[_config.folder & 1]; }

void SysExPage::changed()
{
  _dirty = true;
  _changedMillis = millis();
  markDirty();
}

void SysExPage::load()
{
  Preferences prefs;
  if (!prefs.begin("oximate", true)) return;
  SyxConfig stored;
  if (prefs.getBytesLength("syx") == sizeof(stored) && prefs.getBytes("syx", &stored, sizeof(stored)) == sizeof(stored) &&
      stored.version == SyxConfig::kVersion) {
    _config = stored;
  }
  prefs.end();
}

void SysExPage::save()
{
  Preferences prefs;
  if (prefs.begin("oximate", false)) {
    prefs.putBytes("syx", &_config, sizeof(_config));
    prefs.end();
  }
}

void SysExPage::status(const char *text)
{
  strncpy(_status, text, sizeof(_status) - 1);
  _status[sizeof(_status) - 1] = '\0';
  markDirty();
}

// ---------------------------------------------------------------------------- buttons and touch

void SysExPage::buttonLabels(const char *labels[3]) const
{
  SysExSender::Progress p = _sender.progress();
  switch (_view) {
    case View::Library:
      labels[0] = "Up";
      labels[1] = "Load";
      labels[2] = "Down";
      break;
    case View::Send:
      if (p.state == SysExSender::State::Sending) {
        labels[0] = "Cancel";
        labels[1] = "";
        labels[2] = "";
      } else {
        labels[0] = "Back";
        labels[1] = _messageCount ? "Send" : "";
        labels[2] = "";
      }
      break;
    case View::Capture:
      labels[0] = _capture.status().active ? "Stop" : "Start";
      labels[1] = "Save";
      labels[2] = "Discard";
      break;
  }
}

void SysExPage::onButton(int index)
{
  switch (_view) {
    case View::Library:
      if (index == 0 && _selected > 0) _selected--;
      if (index == 2 && _selected < _fileCount - 1) _selected++;
      if (index == 1) loadSelected();
      markDirty();
      break;
    case View::Send:
      if (_sender.busy()) {
        if (index == 0) {
          _sender.cancel();
          status("Cancelling after this message...");
        }
      } else if (index == 0) {
        _view = _fromCapture ? View::Capture : View::Library;
        markDirty();
      } else if (index == 1) {
        startSend();
      }
      break;
    case View::Capture:
      if (index == 0) {
        if (_capture.status().active) {
          _capture.stop();
          status("Stopped");
        } else if (!_capture.ready()) {
          status("No memory for captures");
        } else {
          _capture.start(_config.captureInputs, _config.captureOnly);
          _savedName[0] = '\0';
          status("Waiting for SysEx...");
        }
      } else if (index == 1) {
        saveCapture();
      } else {
        _capture.clear();
        _savedName[0] = '\0';
        status("Discarded");
      }
      break;
  }
}

void SysExPage::onButtonHold(int index)
{
  if (_view == View::Capture && index == 1) sendCapture(); // hold B: send the capture back
}

void SysExPage::onTouch(int x, int y)
{
  if (_view == View::Library) touchLibrary(x, y);
  else if (_view == View::Capture) touchCapture(x, y);
}

void SysExPage::touchLibrary(int x, int y)
{
  if (y >= kListY && y < kListY + kRows * kRowH) {
    int first = _selected >= kRows ? _selected - kRows + 1 : 0;
    int row = first + (y - kListY) / kRowH;
    if (row < _fileCount) {
      if (row == _selected) loadSelected(); // tap the selected file again: load it
      _selected = row;
      markDirty();
    }
  } else if (y >= kChipsY && y < kChipsY + kChipH) {
    switch (x / kChipW) {
      case 0: _config.outputs = _config.outputs % 3 + 1; break; // DIN, USB, both
      case 1: _config.gapIndex = (_config.gapIndex + 1) % (sizeof(kGaps) / sizeof(kGaps[0])); break;
      case 2:
        _config.folder ^= 1;
        openFolder();
        break;
      default:
        _view = View::Capture;
        markDirty();
        return;
    }
    changed();
  }
}

void SysExPage::touchCapture(int x, int y)
{
  if (y < kCaptureChipsY || y >= kCaptureChipsY + kChipH) return;
  switch (x / kChipW) {
    case 0: _config.captureInputs ^= SysExCapture::kDin; break;
    case 1: _config.captureInputs ^= SysExCapture::kUsb; break;
    case 2: _config.captureOnly = !_config.captureOnly; break;
    default:
      _view = View::Library; // back to the library
      openFolder();
      return;
  }
  changed();
}

void SysExPage::enter()
{
  Page::enter();
  _leds.claim(kLedOwner, 0, Leds::kCount);
  if (_view == View::Library) openFolder();
}

void SysExPage::leave() { _leds.release(kLedOwner); }

// ---------------------------------------------------------------------------- files

void SysExPage::openFolder()
{
  _fileCount = SdStore::mounted() ? SdStore::list(folder(), ".syx", _names, kFiles) : 0;
  if (_selected >= _fileCount) _selected = _fileCount ? _fileCount - 1 : 0;
  status(SdStore::mounted() ? (_fileCount ? "" : "No .syx files here") : "No SD card (FAT32 needed)");
}

void SysExPage::loaded(bool ok, size_t length, void *ctx)
{
  SysExPage *page = static_cast<SysExPage *>(ctx);
  page->_readLength = length;
  page->_result = ok ? 1 : -1;
}

void SysExPage::saved(bool ok, void *ctx) { static_cast<SysExPage *>(ctx)->_result = ok ? 1 : -1; }

void SysExPage::loadSelected()
{
  if (!_fileCount) return;
  if (!_sendBuffer || !_messages) return status("No memory for sending");
  if (_sender.busy() || _step != SdStep::Idle || SdStore::busy()) return status("Busy, try again");
  snprintf(_path, sizeof(_path), "%s/%s", folder(), _names[_selected]);
  _result = 0;
  if (!SdStore::readFile(_path, _sendBuffer, kSendCapacity, loaded, this)) return status("SD busy");
  _step = SdStep::Reading;
  status("Loading...");
}

/*! A file (or the capture) is in memory: list its messages and show what a send would do */
void SysExPage::parseLoaded(const uint8_t *data, size_t length, const char *name)
{
  _sendData = data;
  _messageCount = Syx::split(data, length, _messages, kMaxMessages, _stats);
  _broadcast = false;
  for (int i = 0; i < _messageCount; i++) {
    if (Syx::isBroadcast(data + _messages[i].offset, _messages[i].length)) _broadcast = true;
  }
  strncpy(_sendName, name, sizeof(_sendName) - 1);
  _sendName[sizeof(_sendName) - 1] = '\0';
  _view = View::Send;
  if (!_messageCount) status("No complete SysEx message in it");
  else if (_stats.messages > (uint32_t)_messageCount) status("Too many messages: sends the first 1024");
  else status("");
  markFooterDirty();
  markDirty();
}

void SysExPage::startSend()
{
  if (!_messageCount) return;
  uint16_t gap = kGaps[_config.gapIndex % (sizeof(kGaps) / sizeof(kGaps[0]))];
  if (!_sender.start(_sendData, _messages, _messageCount, _config.outputs, gap)) return status("Busy");
  status("");
  markFooterDirty();
}

void SysExPage::sendCapture()
{
  SysExCapture::Status s = _capture.status();
  if (s.active || !s.bytes) return status(s.active ? "Stop the capture first" : "Nothing captured");
  if (!_messages || _sender.busy()) return status("Busy");
  _fromCapture = true;
  parseLoaded(_capture.data(), s.bytes, "capture (not saved)");
}

void SysExPage::saveCapture()
{
  SysExCapture::Status s = _capture.status();
  if (s.active) return status("Stop the capture first");
  if (!s.bytes) return status("Nothing captured");
  if (!SdStore::mounted()) return status("No SD card: hold B to send it back instead");
  if (_step != SdStep::Idle || SdStore::busy()) return status("Busy, try again");
  m5::rtc_datetime_t now = M5.Rtc.getDateTime();
  _config.counter++;
  Syx::fileName(_savedName, sizeof(_savedName), Syx::slug(_capture.data(), s.bytes), now.date.year, now.date.month,
                now.date.date, now.time.hours, now.time.minutes, now.time.seconds, _config.counter);
  snprintf(_path, sizeof(_path), "%s/%s", kDumpFolder, _savedName);
  _result = 0;
  if (!SdStore::writeFile(_path, _capture.data(), s.bytes, saved, this)) return status("SD busy");
  _step = SdStep::Writing;
  changed(); // the counter
  status("Saving...");
}

// ---------------------------------------------------------------------------- every loop

void SysExPage::poll()
{
  if (_capture.poll(millis(), _config.idleSeconds * 1000u)) status("Capture ended (idle)");

  if (_step == SdStep::Reading && _result != 0) {
    _step = SdStep::Idle;
    if (_result > 0) {
      _fromCapture = false;
      parseLoaded(_sendBuffer, _readLength, _names[_selected]);
    } else {
      status("Load failed");
    }
  } else if (_step == SdStep::Writing && _result != 0) {
    _step = SdStep::Idle;
    char text[48];
    snprintf(text, sizeof(text), _result > 0 ? "Saved %s" : "Save failed: %s", _savedName);
    status(text);
  }

  if (_dirty && !_sender.busy() && millis() - _changedMillis >= kSaveDelayMillis) {
    save();
    _dirty = false;
  }
}

void SysExPage::tick()
{
  renderLeds();
  SysExSender::Progress p = _sender.progress();
  SysExCapture::Status s = _capture.status();
  if (s.bytes != _lastBytes) {
    _lastBytes = s.bytes;
    _flickerUntil = millis() + 60;
  }
  bool changedState = p.state != _shownProgress.state || s.active != _shownCapture.active;
  if (changedState) markFooterDirty();
  if (millis() - _lastDraw >= kRedrawMillis &&
      (changedState || p.sentBytes != _shownProgress.sentBytes || s.bytes != _shownCapture.bytes ||
       (s.active && s.lastByteMillis))) {
    _lastDraw = millis();
    markDirty();
  }
}

void SysExPage::renderLeds()
{
  SysExSender::Progress p = _sender.progress();
  SysExCapture::Status s = _capture.status();
  for (int i = 0; i < Leds::kCount; i++) {
    RGBColor color(0, 0, 0);
    if (p.state == SysExSender::State::Sending && p.totalBytes) {
      int lit = (int)((uint64_t)p.sentBytes * Leds::kCount / p.totalBytes);
      color = i <= lit ? RGBColor(0, 200, 255) : RGBColor(0, 20, 30);
    } else if (s.active) {
      // A slow red pulse; the input's side flickers with incoming bytes (0-4 USB, 5-9 DIN)
      uint8_t level = (uint8_t)(40 + 40 * sinf(millis() / 300.0f));
      color = RGBColor(level, 0, 0);
      if ((int32_t)(_flickerUntil - millis()) > 0) color = RGBColor(255, 40, 40);
    }
    _leds.setPixel(kLedOwner, i, color);
  }
}

// ---------------------------------------------------------------------------- drawing

void SysExPage::draw()
{
  _shownProgress = _sender.progress();
  _shownCapture = _capture.status();
  M5Canvas &c = canvas();
  c.fillRect(0, kContentTop, c.width(), kContentHeight, TFT_BLACK);
  switch (_view) {
    case View::Library: drawLibrary(); break;
    case View::Send: drawSend(); break;
    default: drawCapture(); break;
  }
  if (_status[0]) { // under the list in the library, at the bottom elsewhere
    c.setTextColor(TFT_YELLOW, TFT_BLACK);
    c.setCursor(4, _view == View::Library ? kListY + kRows * kRowH + 4 : 202);
    c.print(_status);
  }
}

void SysExPage::drawLibrary()
{
  M5Canvas &c = canvas();
  char text[48];
  snprintf(text, sizeof(text), " %s/", folder());
  c.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  c.setCursor(0, kPathY);
  c.print(text);
  int first = _selected >= kRows ? _selected - kRows + 1 : 0;
  for (int row = 0; row < kRows && first + row < _fileCount; row++) {
    int i = first + row;
    int y = kListY + row * kRowH;
    bool selected = i == _selected;
    if (selected) c.fillRect(0, y, c.width(), kRowH, TFT_WHITE);
    c.setTextColor(selected ? TFT_BLACK : TFT_WHITE, selected ? TFT_WHITE : TFT_BLACK);
    c.setCursor(4, y + 1);
    c.print(_names[i]);
  }
  char gap[12];
  snprintf(gap, sizeof(gap), "Gap %ums", kGaps[_config.gapIndex % (sizeof(kGaps) / sizeof(kGaps[0]))]);
  drawChip(c, 0 * kChipW, kChipsY, kChipW, kChipH, kOutputNames[_config.outputs & 3], true);
  drawChip(c, 1 * kChipW, kChipsY, kChipW, kChipH, gap, true);
  drawChip(c, 2 * kChipW, kChipsY, kChipW, kChipH, _config.folder ? "Dumps" : "/syx", true);
  drawChip(c, 3 * kChipW, kChipsY, kChipW, kChipH, "Capture", true);
}

void SysExPage::drawSend()
{
  M5Canvas &c = canvas();
  SysExSender::Progress p = _shownProgress;
  char text[64];
  c.setTextColor(TFT_WHITE, TFT_BLACK);
  c.setCursor(4, kPathY);
  c.print(_sendName);

  uint32_t bytes = 0;
  for (int i = 0; i < _messageCount; i++) bytes += _messages[i].length;
  uint16_t gap = kGaps[_config.gapIndex % (sizeof(kGaps) / sizeof(kGaps[0]))];
  // DIN carries ~3125 bytes per second
  float seconds = bytes / 3125.0f + (_messageCount > 1 ? (_messageCount - 1) * gap / 1000.0f : 0);
  snprintf(text, sizeof(text), "%d msg  %lu bytes  ~%.1f s", _messageCount, (unsigned long)bytes, seconds);
  c.setCursor(4, kPathY + 18);
  c.print(text);
  if (_messageCount) {
    snprintf(text, sizeof(text), "%s, to %s", Syx::manufacturer(_sendData + _messages[0].offset, _messages[0].length),
             kOutputLong[_config.outputs & 3]);
    c.setCursor(4, kPathY + 36);
    c.print(text);
  }
  if (_stats.stray || _stats.truncated) {
    snprintf(text, sizeof(text), "%lu stray bytes skipped%s", (unsigned long)_stats.stray,
             _stats.truncated ? ", last msg cut" : "");
    c.setTextColor(TFT_ORANGE, TFT_BLACK);
    c.setCursor(4, kPathY + 54);
    c.print(text);
  }
  if (_broadcast) {
    c.setTextColor(TFT_RED, TFT_BLACK);
    c.setCursor(4, kPathY + 72);
    c.print("Broadcast: every device reacts");
    c.setCursor(4, kPathY + 90);
    c.print((_config.outputs & SysExSender::kDin) ? "(also the SAM2695 synth)" : "");
  }

  if (p.state == SysExSender::State::Idle) return;
  int barY = 140;
  int width = p.totalBytes ? (int)((uint64_t)p.sentBytes * 312 / p.totalBytes) : 0;
  c.drawRect(4, barY, 312, 12, TFT_DARKGREY);
  c.fillRect(4, barY, width, 12, TFT_CYAN);
  snprintf(text, sizeof(text), "Message %lu / %lu   %lu / %lu bytes", (unsigned long)(p.message + 1),
           (unsigned long)p.messages, (unsigned long)p.sentBytes, (unsigned long)p.totalBytes);
  c.setTextColor(TFT_WHITE, TFT_BLACK);
  c.setCursor(4, barY + 18);
  c.print(text);
  const char *state = "";
  switch (p.state) {
    case SysExSender::State::Done: state = "Done."; break;
    case SysExSender::State::Cancelled: state = "Cancelled after a complete message."; break;
    case SysExSender::State::NoOutput: state = "No USB device connected."; break;
    default: break;
  }
  c.setCursor(4, barY + 36);
  c.print(state);
}

void SysExPage::drawCapture()
{
  M5Canvas &c = canvas();
  SysExCapture::Status s = _shownCapture;
  drawChip(c, 0 * kChipW, kCaptureChipsY, kChipW, kChipH, "In DIN", _config.captureInputs & SysExCapture::kDin);
  drawChip(c, 1 * kChipW, kCaptureChipsY, kChipW, kChipH, "In USB", _config.captureInputs & SysExCapture::kUsb);
  drawChip(c, 2 * kChipW, kCaptureChipsY, kChipW, kChipH, _config.captureOnly ? "Only on" : "Only off",
           _config.captureOnly);
  drawChip(c, 3 * kChipW, kCaptureChipsY, kChipW, kChipH, "Library", true);

  char text[64];
  const char *state = s.active ? (s.bytes ? "Capturing..." : "Waiting for SysEx...")
                               : (s.full ? "Stopped: buffer full (512 KB)" : (s.bytes ? "Captured" : "Idle"));
  c.setTextColor(s.active ? TFT_RED : TFT_WHITE, TFT_BLACK);
  c.setCursor(4, kLineY);
  c.print(state);
  c.setTextColor(TFT_WHITE, TFT_BLACK);
  if (s.bytes) {
    snprintf(text, sizeof(text), "%s  %lu msg  %lu bytes", Syx::manufacturer(_capture.data(), s.bytes),
             (unsigned long)s.messages, (unsigned long)s.bytes);
    c.setCursor(4, kLineY + 20);
    c.print(text);
  }
  if (s.active && s.lastByteMillis) {
    snprintf(text, sizeof(text), "last byte %.1f s ago", (millis() - s.lastByteMillis) / 1000.0f);
    c.setCursor(4, kLineY + 40);
    c.print(text);
  }
  if (_savedName[0]) {
    snprintf(text, sizeof(text), "File: %s", _savedName);
    c.setCursor(4, kLineY + 60);
    c.print(text);
  }
  c.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  c.setCursor(4, kLineY + 84);
  c.print("Hold B: send the capture back");
}
