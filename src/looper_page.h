#pragma once

#include <atomic>
#include "clock_engine.h"
#include "leds.h"
#include "looper.h"
#include "sd_store.h"
#include "ui.h"

/*! Loop page (stage 13): state and position, an overview of the loop's busiest channels,
 *  settings chips, undo / clear, and saving / loading loops as MIDI files on the SD card.
 *  A = Rec (arm, disarm, cancel the first pass; hold: undo), B = playback on/off,
 *  C = overdub on/off. Load opens a file list (tap a file to load it, A goes back). */
class LooperPage : public Page
{
public:
  LooperPage(Looper &looper, ClockEngine &clock, Leds &leds) : _looper(looper), _clock(clock), _leds(leds) {}

  const char *title() const override { return "Loop"; }
  void buttonLabels(const char *labels[3]) const override;
  void onButton(int index) override;
  void onButtonHold(int index) override;
  void onTouch(int x, int y) override;
  void enter() override;
  void leave() override;
  void tick() override;
  void draw() override;

private:
  static constexpr uint8_t kLedOwner = 13;
  static constexpr int kFiles = 9;
  static constexpr size_t kLoadCapacity = 256 * 1024;

  void drawMain();
  void drawFiles();
  void touchMain(int x, int y);
  void startSave();
  void continueSave();
  void openFiles();
  void startLoad(int index);
  void finishLoad();
  void status(const char *text);
  void renderLeds(const LoopSnapshot &snap);
  void freeFile();

  static void saved(bool ok, void *ctx);
  static void loaded(bool ok, size_t length, void *ctx);

  Looper &_looper;
  ClockEngine &_clock;
  Leds &_leds;

  bool _filesView = false;
  char _names[kFiles][SdStore::kNameLength];
  int _fileCount = 0;
  char _status[48] = "";

  // Save and load run partly on the SD task: these flags hand the results back
  enum class FileStep : uint8_t
  {
    Idle,
    Exporting, ///< waiting for the looper's copy
    Writing,
    Reading,
  };
  FileStep _step = FileStep::Idle;
  uint8_t *_file = nullptr; ///< SMF bytes in PSRAM while saving or loading
  char _path[64];
  std::atomic<int> _result{0}; ///< 0 = pending, 1 = done, -1 = failed
  std::atomic<size_t> _readLength{0};

  LoopSnapshot _shown = {};
  uint32_t _lastDraw = 0;
};
