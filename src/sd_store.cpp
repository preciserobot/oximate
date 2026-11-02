#include "sd_store.h"

#include <Arduino.h>
#include <SD.h>
#include <algorithm>
#include <atomic>
#include <string.h>

#include "sd_check.h"
#include "spi_bus.h"
#include "sync.h"

namespace
{
enum class JobType : uint8_t
{
  None,
  Write,
  Read,
};

struct Job
{
  JobType type = JobType::None;
  char path[SdStore::kNameLength * 2];
  const uint8_t *data;
  uint8_t *buffer;
  size_t length; // write: bytes to write; read: capacity
  SdStore::WriteDone writeDone;
  SdStore::ReadDone readDone;
  void *ctx;
};

CriticalSection gLock;
Job gJob;                       // guarded by gLock while queued
std::atomic<bool> gBusy{false}; // a job is queued or running
SdStore::TransportRunning gRunning = nullptr;
TaskHandle_t gTask = nullptr;

size_t chunk() { return gRunning && gRunning() ? SdStore::kChunkRunning : SdStore::kChunkStopped; }

/*! Create every folder on the way to a file */
void makeFolders(const char *path)
{
  char folder[sizeof(Job::path)];
  for (const char *p = strchr(path + 1, '/'); p; p = strchr(p + 1, '/')) {
    size_t n = p - path;
    if (n >= sizeof(folder)) return;
    memcpy(folder, path, n);
    folder[n] = '\0';
    SpiLock bus("sd mkdir");
    if (!SD.exists(folder)) SD.mkdir(folder);
  }
}

bool doWrite(const Job &job)
{
  makeFolders(job.path);
  File file;
  {
    SpiLock bus("sd open");
    file = SD.open(job.path, FILE_WRITE);
  }
  if (!file) return false;
  size_t done = 0;
  bool ok = true;
  while (ok && done < job.length) {
    size_t n = std::min(chunk(), job.length - done);
    {
      SpiLock bus("sd write");
      ok = file.write(job.data + done, n) == n;
    }
    done += n;
    vTaskDelay(1); // USB polls and screen slices get the bus in between
  }
  SpiLock bus("sd close"); // the FAT update: the longest hold, once per file
  file.close();
  return ok;
}

bool doRead(const Job &job, size_t &length)
{
  File file;
  {
    SpiLock bus("sd open");
    file = SD.open(job.path, FILE_READ);
  }
  length = 0;
  if (!file) return false;
  bool ok = true;
  while (ok && length < job.length) {
    size_t n = std::min(chunk(), job.length - length);
    int got;
    {
      SpiLock bus("sd read");
      got = file.read(job.buffer + length, n);
    }
    if (got <= 0) break;
    length += got;
    vTaskDelay(1);
  }
  SpiLock bus("sd close");
  file.close();
  return ok;
}

void run(void *)
{
  for (;;) {
    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(100));
    Job job;
    {
      CriticalGuard guard(gLock);
      job = gJob;
      gJob.type = JobType::None;
    }
    if (job.type == JobType::Write) {
      bool ok = doWrite(job);
      gBusy = false;
      if (job.writeDone) job.writeDone(ok, job.ctx);
    } else if (job.type == JobType::Read) {
      size_t length = 0;
      bool ok = doRead(job, length);
      gBusy = false;
      if (job.readDone) job.readDone(ok, length, job.ctx);
    }
  }
}

bool queue(const Job &job)
{
  if (!SdStore::mounted() || gBusy.exchange(true)) return false;
  {
    CriticalGuard guard(gLock);
    gJob = job;
  }
  if (gTask) xTaskNotifyGive(gTask);
  return true;
}
} // namespace

void SdStore::begin(TransportRunning running)
{
  gRunning = running;
  if (!gTask) xTaskCreatePinnedToCore(::run, "sd", 6144, nullptr, 1, &gTask, 1);
}

bool SdStore::mounted() { return SdCheck::result().present; }
bool SdStore::busy() { return gBusy; }

bool SdStore::writeFile(const char *path, const uint8_t *data, size_t length, WriteDone done, void *ctx)
{
  Job job;
  job.type = JobType::Write;
  strncpy(job.path, path, sizeof(job.path) - 1);
  job.path[sizeof(job.path) - 1] = '\0';
  job.data = data;
  job.buffer = nullptr;
  job.length = length;
  job.writeDone = done;
  job.readDone = nullptr;
  job.ctx = ctx;
  return queue(job);
}

bool SdStore::readFile(const char *path, uint8_t *buffer, size_t capacity, ReadDone done, void *ctx)
{
  Job job;
  job.type = JobType::Read;
  strncpy(job.path, path, sizeof(job.path) - 1);
  job.path[sizeof(job.path) - 1] = '\0';
  job.data = nullptr;
  job.buffer = buffer;
  job.length = capacity;
  job.writeDone = nullptr;
  job.readDone = done;
  job.ctx = ctx;
  return queue(job);
}

int SdStore::list(const char *dir, const char *suffix, char (*names)[kNameLength], int max)
{
  if (!mounted() || max <= 0) return 0;
  File folder;
  {
    SpiLock bus("sd list");
    folder = SD.open(dir);
  }
  if (!folder || !folder.isDirectory()) return 0;
  int count = 0;
  size_t suffixLength = strlen(suffix);
  for (;;) {
    File entry;
    {
      SpiLock bus("sd list");
      entry = folder.openNextFile();
    }
    if (!entry) break;
    const char *name = entry.name();
    size_t n = strlen(name);
    if (!entry.isDirectory() && n >= suffixLength && strcasecmp(name + n - suffixLength, suffix) == 0 && count < max) {
      strncpy(names[count], name, kNameLength - 1);
      names[count][kNameLength - 1] = '\0';
      count++;
    }
    SpiLock bus("sd list");
    entry.close();
  }
  {
    SpiLock bus("sd list");
    folder.close();
  }
  // Newest first: names carry a running number, so descending order puts the latest on top
  for (int i = 1; i < count; i++) {
    char name[kNameLength];
    memcpy(name, names[i], kNameLength);
    int j = i;
    while (j > 0 && strcmp(names[j - 1], name) < 0) {
      memcpy(names[j], names[j - 1], kNameLength);
      j--;
    }
    memcpy(names[j], name, kNameLength);
  }
  return count;
}
