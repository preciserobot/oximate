#include "spi_bus.h"

#if defined(ESP_PLATFORM)
#include <atomic>

static SemaphoreHandle_t gMutex = nullptr;
static std::atomic<bool> gWanted{false}; // the MIDI task found the bus busy and is waiting for it

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

void SpiBus::lock()
{
  // The display releases the bus between drawing chunks; without this it would take it straight
  // back and the MIDI task (polling every 1 ms) would almost never get a turn
  for (int i = 0; i < 3 && gWanted; i++) vTaskDelay(1);
  xSemaphoreTake(gMutex, portMAX_DELAY);
}
void SpiBus::unlock() { xSemaphoreGive(gMutex); }
#endif
