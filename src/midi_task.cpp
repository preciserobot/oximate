#include "midi_task.h"

#include <string.h>

#include "clock_engine.h"
#include "din_midi.h"
#include "platform_time.h"
#include "usb_midi.h"

void EventRing::push(const MidiMessage &msg, bool forwarded)
{
  size_t head = _head.load(std::memory_order_relaxed);
  size_t next = (head + 1) % kLength;
  if (next == _tail.load(std::memory_order_acquire)) {
    _dropped++;
    return;
  }
  MidiEvent &event = _events[head];
  event.timeMicros = msg.timeMicros;
  event.source = msg.source;
  event.cable = msg.cable;
  event.forwarded = forwarded;
  event.length = msg.length;
  event.count = msg.length < MidiEvent::kMaxBytes ? msg.length : MidiEvent::kMaxBytes;
  memcpy(event.bytes, msg.bytes, event.count);
  _head.store(next, std::memory_order_release);
}

bool EventRing::pop(MidiEvent &event)
{
  size_t tail = _tail.load(std::memory_order_relaxed);
  if (tail == _head.load(std::memory_order_acquire)) {
    return false;
  }
  event = _events[tail];
  _tail.store((tail + 1) % kLength, std::memory_order_release);
  return true;
}

#if defined(ESP_PLATFORM)
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

static EventRing gEvents;
static UsbMidi *gUsb = nullptr;
static DinMidi *gDin = nullptr;
static ClockEngine *gClock = nullptr;
static TaskHandle_t gTask = nullptr;
static esp_timer_handle_t gTimer = nullptr;

/*! esp_timer callback: only wakes the MIDI task, which then runs the due work */
static void onTimer(void *) { xTaskNotifyGive(gTask); }

static void run(void *)
{
  for (;;) {
    // Sleep until the next clock event if that comes before the 1 ms polling tick
    uint64_t now = nowMicros64();
    uint64_t next = gClock->nextDueUs();
    if (next > now && next - now < 1000) {
      esp_timer_stop(gTimer); // fails harmlessly when not running
      esp_timer_start_once(gTimer, next - now);
    }
    ulTaskNotifyTake(pdTRUE, 1);

    gClock->runDue(nowMicros64());
    gDin->poll();
    gUsb->poll();
    gDin->pumpTx();
  }
}

void MidiTask::start(UsbMidi &usb, DinMidi &din, ClockEngine &clock)
{
  gUsb = &usb;
  gDin = &din;
  gClock = &clock;
  esp_timer_create_args_t args = {};
  args.callback = onTimer;
  args.name = "midi-clock";
  esp_timer_create(&args, &gTimer); // before the task: it arms the timer straight away
  xTaskCreatePinnedToCore(run, "midi", kStackBytes, nullptr, kPriority, &gTask, kCore);
}

EventRing &MidiTask::events() { return gEvents; }
#endif
