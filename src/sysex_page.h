#pragma once

#include <atomic>
#include "leds.h"
#include "sd_store.h"
#include "sysex_capture.h"
#include "sysex_sender.h"
#include "syx_file.h"
#include "ui.h"

/*! Settings of the SysEx page, saved in NVS (key "syx"). Bump kVersion when the layout changes. */
struct SyxConfig
{
  static constexpr uint8_t kVersion = 1;

  uint8_t version = kVersion;
  uint8_t outputs = SysExSender::kDin; ///< 1 DIN, 2 USB, 3 both; every send names one
  uint8_t gapIndex = 3;                ///< into kGaps: 20 ms
  uint8_t folder = 0;                  ///< 0 = /syx, 1 = /oximate/dumps
  uint8_t captureInputs = SysExCapture::kDin | SysExCapture::kUsb;
  bool captureOnly = true;  ///< don't forward SysEx from a capturing input
  uint8_t idleSeconds = 2;  ///< a capture ends this long after its last byte
  uint16_t counter = 0;     ///< file names while the RTC isn't set
};

/*! SysEx page (stage 14): send .syx files from the SD card, capture dumps and save them.
 *  Library: A / C move the selection, B loads the file and shows what will be sent.
 *  Send: B sends (A back); while sending A cancels after the current message.
 *  Capture: A starts / stops, B saves (hold: send the capture back), C discards.
 *  poll() runs every loop on any page: it ends idle captures and finishes SD jobs. */
class SysExPage : public Page
{
public:
  SysExPage(SysExSender &sender, SysExCapture &capture, Leds &leds) : _sender(sender), _capture(capture), _leds(leds) {}

  void begin(); ///< buffers in PSRAM, settings from NVS, capture buffer for the capture
  void poll();

  const char *title() const override { return "SysEx"; }
  void buttonLabels(const char *labels[3]) const override;
  void onButton(int index) override;
  void onButtonHold(int index) override;
  void onTouch(int x, int y) override;
  void enter() override;
  void leave() override;
  void tick() override;
  void draw() override;

  static constexpr uint16_t kGaps[] = {0, 2, 10, 20, 50, 100, 250};

private:
  enum class View : uint8_t
  {
    Library,
    Send,
    Capture,
  };
  static constexpr uint8_t kLedOwner = 14;
  static constexpr size_t kSendCapacity = 512 * 1024;
  static constexpr size_t kCaptureCapacity = 512 * 1024;
  static constexpr int kMaxMessages = 1024;
  static constexpr int kFiles = 32;
  static constexpr int kRows = 7;

  void drawLibrary();
  void drawSend();
  void drawCapture();
  void touchLibrary(int x, int y);
  void touchCapture(int x, int y);
  void openFolder();
  void loadSelected();
  void parseLoaded(const uint8_t *data, size_t length, const char *name);
  void startSend();
  void sendCapture();
  void saveCapture();
  void status(const char *text);
  void changed();
  void load();
  void save();
  void renderLeds();
  const char *folder() const;

  static void loaded(bool ok, size_t length, void *ctx);
  static void saved(bool ok, void *ctx);

  SysExSender &_sender;
  SysExCapture &_capture;
  Leds &_leds;
  SyxConfig _config;
  bool _dirty = false;
  uint32_t _changedMillis = 0;

  View _view = View::Library;
  char _names[kFiles][SdStore::kNameLength];
  int _fileCount = 0;
  int _selected = 0;
  char _status[48] = "";

  uint8_t *_sendBuffer = nullptr;     // PSRAM: the file (or capture) being sent
  uint8_t *_captureBuffer = nullptr;  // PSRAM: the capture
  Syx::Message *_messages = nullptr;  // PSRAM: message list of the send buffer
  const uint8_t *_sendData = nullptr; // what start() sends: the send buffer or the capture
  int _messageCount = 0;
  Syx::Stats _stats;
  char _sendName[SdStore::kNameLength] = "";
  bool _broadcast = false;
  bool _fromCapture = false;

  enum class SdStep : uint8_t
  {
    Idle,
    Reading,
    Writing,
  };
  SdStep _step = SdStep::Idle;
  std::atomic<int> _result{0}; ///< 0 = pending, 1 = done, -1 = failed
  std::atomic<size_t> _readLength{0};
  char _path[80];
  char _savedName[SdStore::kNameLength] = "";

  SysExSender::Progress _shownProgress = {};
  SysExCapture::Status _shownCapture = {};
  uint32_t _lastDraw = 0;
  uint32_t _lastBytes = 0;
  uint32_t _flickerUntil = 0;
};
