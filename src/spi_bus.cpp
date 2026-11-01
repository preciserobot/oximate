#include "spi_bus.h"

#if defined(ESP_PLATFORM)
#include <atomic>
#include <esp_timer.h>

static SemaphoreHandle_t gMutex = nullptr;
static std::atomic<bool> gWanted{false}; // the MIDI task found the bus busy and is waiting for it

// Hold-time statistics for blocking (UI) locks
static uint32_t gLockedAt = 0;
static const char *gHolder = "";
static uint32_t gMaxHold = 0;
static const char *gMaxHolder = "";

void SpiBus::begin()
{
  if (!gMutex) {
    gMutex = xSemaphoreCreateMutex();
  }
}

bool SpiBus::tryLock()
{
  if (xSemaphoreTake(gMutex, 0) == pdTRUE) {
    gWanted = false;
    return true;
  }
  gWanted = true;
  return false;
}

void SpiBus::lock(const char *holder)
{
  // The display releases the bus between drawing chunks; without this it would take it straight
  // back and the MIDI task (polling every 1 ms) would almost never get a turn
  for (int i = 0; i < 3 && gWanted; i++) vTaskDelay(1);
  xSemaphoreTake(gMutex, portMAX_DELAY);
  gLockedAt = esp_timer_get_time();
  gHolder = holder;
}

void SpiBus::unlock()
{
  if (gLockedAt) {
    uint32_t held = esp_timer_get_time() - gLockedAt;
    if (held > gMaxHold) {
      gMaxHold = held;
      gMaxHolder = gHolder;
    }
    gLockedAt = 0;
  }
  xSemaphoreGive(gMutex);
}

uint32_t SpiBus::maxHoldMicros() { return gMaxHold; }
const char *SpiBus::maxHolder() { return gMaxHolder; }
void SpiBus::resetStats() { gMaxHold = 0; }
#endif
