#pragma once

#include <stddef.h>
#include <stdint.h>

/*! SD card access for the looper (stage 13) and the SysEx librarian (stage 14).
 *
 *  The Core2's SD slot shares the SPI bus with the display and the USB host module, so all card
 *  access holds SpiLock in small chunks: ~1 KB while the clock transport runs (a 4 KB write plus
 *  flush held the bus ~12.5 ms on the first card), 4 KB while stopped. File reads and writes run
 *  as jobs on a low-priority SD task on core 1, never in the MIDI task; one job at a time.
 *  The card is mounted once at boot by SdCheck. */
namespace SdStore
{
using TransportRunning = bool (*)();
using WriteDone = void (*)(bool ok, void *ctx);
using ReadDone = void (*)(bool ok, size_t length, void *ctx);

constexpr size_t kChunkRunning = 1024;
constexpr size_t kChunkStopped = 4096;
constexpr int kNameLength = 40;

/*! Start the SD task (after SdCheck::run()). `running` tells it which chunk size to use. */
void begin(TransportRunning running);
bool mounted();
bool busy(); ///< a job is queued or running

/*! Write `length` bytes to `path` (folders are created). `data` must stay valid until `done`,
 *  which is called on the SD task. False if not mounted or another job is pending. */
bool writeFile(const char *path, const uint8_t *data, size_t length, WriteDone done, void *ctx);
/*! Read up to `capacity` bytes of `path` into `buffer`; `done` gets the length read */
bool readFile(const char *path, uint8_t *buffer, size_t capacity, ReadDone done, void *ctx);

/*! Files in `dir` ending in `suffix` (e.g. ".mid"), names only, newest name first (names sort
 *  by their running number); synchronous, holds the bus per directory entry. Returns the count. */
int list(const char *dir, const char *suffix, char (*names)[kNameLength], int max);
} // namespace SdStore
