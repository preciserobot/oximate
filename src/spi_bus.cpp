#include "spi_bus.h"

#if defined(ESP_PLATFORM)
static SemaphoreHandle_t gMutex = nullptr;

void SpiBus::begin()
{
  if (!gMutex) {
    gMutex = xSemaphoreCreateMutex();
  }
}

bool SpiBus::tryLock() { return xSemaphoreTake(gMutex, 0) == pdTRUE; }
void SpiBus::lock() { xSemaphoreTake(gMutex, portMAX_DELAY); }
void SpiBus::unlock() { xSemaphoreGive(gMutex); }
#endif
