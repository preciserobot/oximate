#include "midi_task.h"

#include <string.h>

#include "din_midi.h"
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
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

static EventRing gEvents;
static UsbMidi *gUsb = nullptr;
static DinMidi *gDin = nullptr;

static void run(void *)
{
  for (;;) {
    // Woken every tick (1 ms); a later hardware timer (clock engine) notifies earlier
    ulTaskNotifyTake(pdTRUE, 1);
    gDin->poll();
    gUsb->poll();
    gDin->pumpTx();
  }
}

void MidiTask::start(UsbMidi &usb, DinMidi &din)
{
  gUsb = &usb;
  gDin = &din;
  xTaskCreatePinnedToCore(run, "midi", kStackBytes, nullptr, kPriority, nullptr, kCore);
}

EventRing &MidiTask::events() { return gEvents; }
#endif
