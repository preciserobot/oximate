#pragma once
// Host stand-in for the ESP32 Preferences (NVS) library, backed by an in-memory map
#include <stddef.h>
#include <string.h>
#include <map>
#include <string>
#include <vector>
inline std::map<std::string, std::vector<uint8_t>> gFakeNvs; // "namespace/key" -> bytes
struct Preferences
{
  std::string ns;
  bool begin(const char *name, bool) { ns = name; return true; }
  void end() {}
  size_t getBytesLength(const char *key)
  {
    auto it = gFakeNvs.find(ns + "/" + key);
    return it == gFakeNvs.end() ? 0 : it->second.size();
  }
  size_t getBytes(const char *key, void *out, size_t n)
  {
    auto it = gFakeNvs.find(ns + "/" + key);
    if (it == gFakeNvs.end() || it->second.size() > n) return 0;
    memcpy(out, it->second.data(), it->second.size());
    return it->second.size();
  }
  bool remove(const char *key) { return gFakeNvs.erase(ns + "/" + key) > 0; }
  size_t putBytes(const char *key, const void *data, size_t n)
  {
    const uint8_t *p = static_cast<const uint8_t *>(data);
    gFakeNvs[ns + "/" + key] = std::vector<uint8_t>(p, p + n);
    return n;
  }
};
