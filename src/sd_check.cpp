#include "sd_check.h"

#include <Arduino.h>
#include <SD.h>
#include <SPI.h>

#include "spi_bus.h"

static constexpr int kSdChipSelect = 4; // Core2 microSD slot
static constexpr uint32_t kSdFrequency = 25000000;
static constexpr size_t kChunk = 4096;  // the most stage 13 writes per bus hold
static const char *const kTestFile = "/oximate-sdcheck.bin";

static SdInfo gResult;

const SdInfo &SdCheck::result() { return gResult; }

SdInfo SdCheck::run()
{
  SdInfo info;
  SpiLock bus("sd check");
  if (!SD.begin(kSdChipSelect, SPI, kSdFrequency)) {
    gResult = info;
    return info;
  }
  switch (SD.cardType()) {
    case CARD_NONE: gResult = info; return info;
    case CARD_MMC: info.type = "MMC"; break;
    case CARD_SD: info.type = "SD"; break;
    case CARD_SDHC: info.type = "SDHC"; break;
    default: info.type = "unknown"; break;
  }
  info.present = true;
  info.totalBytes = SD.totalBytes();
  info.usedBytes = SD.usedBytes();

  // On the heap for the test only (static RAM is short with BLE on, stage 17)
  uint8_t *buffer = (uint8_t *)malloc(2 * kChunk);
  if (!buffer) {
    gResult = info;
    return info;
  }
  uint8_t *readBack = buffer + kChunk;
  for (size_t i = 0; i < kChunk; i++) buffer[i] = (uint8_t)(i * 7 + 3);

  File file = SD.open(kTestFile, FILE_WRITE);
  if (file) {
    uint32_t start = micros();
    size_t written = file.write(buffer, kChunk);
    file.flush();
    info.writeMicros = micros() - start;
    file.close();
    if (written == kChunk) {
      file = SD.open(kTestFile, FILE_READ);
      if (file) {
        start = micros();
        size_t got = file.read(readBack, kChunk);
        info.readMicros = micros() - start;
        file.close();
        info.readBackOk = got == kChunk && memcmp(buffer, readBack, kChunk) == 0;
      }
    }
    SD.remove(kTestFile);
  }
  free(buffer);
  gResult = info;
  return info;
}
