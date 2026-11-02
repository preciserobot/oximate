#pragma once

#include <stdint.h>

/*! One-off SD card check at boot (before stage 13): is a card there, what is it, and how long
 *  does a 4 KB write / read hold the shared SPI bus. The Core2's SD slot shares the bus with
 *  the display and the USB host module, so every access takes SpiLock. */
struct SdInfo
{
  bool present = false;
  const char *type = "none"; ///< MMC, SD, SDHC, unknown
  uint64_t totalBytes = 0;
  uint64_t usedBytes = 0;
  uint32_t writeMicros = 0; ///< one 4 KB chunk, bus held
  uint32_t readMicros = 0;
  bool readBackOk = false;
};

namespace SdCheck
{
SdInfo run(); ///< call in setup(), before the MIDI task starts
const SdInfo &result();
} // namespace SdCheck
