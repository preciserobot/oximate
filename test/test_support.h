#pragma once
// Shared helpers for the host unit tests (make test). Every test file includes this first.
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include <Preferences.h> // fake NVS: gFakeNvs
#include <usbh_midi.h>   // fake device: records sends in gUsbSent

#define private public // white-box access to parsers and internal state
#include "clock_engine.h"
#include "din_midi.h"
#include "midi_outputs.h"
#include "midi_task.h"
#include "note_tracker.h"
#include "panic.h"
#include "router.h"
#include "sam_synth.h"
#include "tap_tempo.h"
#include "usb_midi.h"
#undef private
#include "platform_time.h"


inline int gFailures = 0;
inline int gChecks = 0;
#define CHECK(cond)                                                     \
  do {                                                                  \
    gChecks++;                                                          \
    if (!(cond)) {                                                      \
      gFailures++;                                                      \
      printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);            \
    }                                                                   \
  } while (0)

inline std::vector<std::string> gReceived;
inline std::vector<uint32_t> gReceivedTimes;

inline std::string hex(const uint8_t *bytes, size_t n)
{
  std::string s;
  char b[4];
  for (size_t i = 0; i < n; i++) {
    snprintf(b, sizeof(b), "%02X ", bytes[i]);
    s += b;
  }
  return s;
}

inline void record(const MidiMessage &m)
{
  std::string s = hex(m.bytes, m.length);
  if (m.cable) s += "c" + std::to_string(m.cable);
  gReceived.push_back(s);
  gReceivedTimes.push_back(m.timeMicros);
}

inline void expect(const char *name, std::vector<std::string> expected)
{
  gChecks++;
  if (gReceived != expected) {
    gFailures++;
    printf("FAIL %s\n  got:", name);
    for (auto &s : gReceived) printf(" [%s]", s.c_str());
    printf("\n");
  }
  gReceived.clear();
  gReceivedTimes.clear();
}

inline MidiMessage msg(MidiSource source, std::initializer_list<uint8_t> bytes)
{
  static uint8_t buf[16];
  size_t n = 0;
  for (uint8_t b : bytes) buf[n++] = b;
  return {source, 0, buf, (uint16_t)n};
}


struct OutputRig
{
  HardwareSerial serial;
  DinMidi din;
  UsbMidi usb;
  NoteTracker tracker;
  MidiOutputs outputs;
  ClockEngine clock;
  Router router;

  OutputRig()
  {
    gFakeNvs.clear();
    gFakeMicros64 = 0;
    din.begin(serial, 0, 0, record);
    usb.begin(record);
    outputs.begin(din, usb, tracker);
    clock.begin(outputs);
    router.begin(outputs, clock);
  }
  std::vector<uint8_t> dinBytes()
  {
    for (int i = 0; i < 5000 && !(din._queue.empty() && din._realtime.empty()); i++) {
      serial.drain(128);
      din.pumpTx();
    }
    std::vector<uint8_t> out = serial.out;
    serial.out.clear();
    return out;
  }
};


struct ClockLog
{
  std::vector<ClockEvent> events;
  static void listener(const ClockEvent &e, void *ctx) { static_cast<ClockLog *>(ctx)->events.push_back(e); }
  int count(ClockEvent::Type type) const
  {
    int n = 0;
    for (auto &e : events) n += e.type == type;
    return n;
  }
  std::vector<uint64_t> times(ClockEvent::Type type) const
  {
    std::vector<uint64_t> t;
    for (auto &e : events)
      if (e.type == type) t.push_back(e.timeUs);
    return t;
  }
};

inline int countByte(const std::vector<uint8_t> &bytes, uint8_t b)
{
  int n = 0;
  for (uint8_t x : bytes) n += x == b;
  return n;
}

/*! Run the engine every 100 us from the current fake time for `micros`, pumping DIN output
 *  like the MIDI task does (the test FIFO drains instantly) */
inline OutputRig *gRunRig = nullptr;
inline void runFor(ClockEngine &clock, uint64_t micros)
{
  uint64_t end = gFakeMicros64 + micros;
  for (; gFakeMicros64 < end; gFakeMicros64 += 100) {
    clock.runDue(gFakeMicros64);
    if (gRunRig) {
      gRunRig->serial.drain(128);
      gRunRig->din.pumpTx();
    }
  }
}


inline bool contains(const std::vector<uint8_t> &haystack, std::vector<uint8_t> needle)
{
  return std::search(haystack.begin(), haystack.end(), needle.begin(), needle.end()) != haystack.end();
}

inline std::vector<uint8_t> gsBytes(uint8_t a1, uint8_t a2, uint8_t a3, uint8_t v)
{
  uint8_t b[11];
  SamSynth::gsMessage(b, a1, a2, a3, v);
  return std::vector<uint8_t>(b, b + 11);
}


/*! Test registry: each test/test_*.cpp registers its tests with TEST(name) { ... } */
using TestFunction = void (*)();
struct TestCase
{
  const char *name;
  TestFunction function;
};
inline std::vector<TestCase> &testRegistry()
{
  static std::vector<TestCase> registry;
  return registry;
}
struct RegisterTest
{
  RegisterTest(const char *name, TestFunction function) { testRegistry().push_back({name, function}); }
};
#define TEST(name)                                       \
  static void name();                                    \
  static RegisterTest name##Registration(#name, name);   \
  static void name()
