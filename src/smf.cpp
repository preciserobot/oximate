#include "smf.h"

#include <algorithm>
#include <string.h>

static const uint16_t kDivision = LoopBuffer::kPpqn;

static void put32(Smf::Sink &sink, uint32_t v)
{
  for (int shift = 24; shift >= 0; shift -= 8) sink.put((uint8_t)(v >> shift));
}

static void putVlq(Smf::Sink &sink, uint32_t v)
{
  uint8_t bytes[5];
  int n = 0;
  bytes[n++] = v & 0x7F;
  while (v >>= 7) bytes[n++] = 0x80 | (v & 0x7F);
  while (n--) sink.put(bytes[n]);
}

static uint8_t dataLength(uint8_t status)
{
  uint8_t type = status & 0xF0;
  return (type == 0xC0 || type == 0xD0) ? 1 : 2;
}

/*! One track body (without the MTrk header). channel 0 = all events; meta = tempo + time sig */
static void writeTrackBody(const LoopEvent *events, uint32_t count, const Smf::Info &info, int channel, bool meta,
                           bool includeEvents, Smf::Sink &sink)
{
  uint32_t last = 0;
  if (meta) {
    uint32_t usPerQuarter = (uint32_t)(60e6f / (info.bpm > 0 ? info.bpm : 120.0f));
    putVlq(sink, 0);
    sink.put(0xFF), sink.put(0x51), sink.put(0x03);
    sink.put(usPerQuarter >> 16), sink.put(usPerQuarter >> 8), sink.put(usPerQuarter);
    putVlq(sink, 0);
    sink.put(0xFF), sink.put(0x58), sink.put(0x04);
    sink.put(info.beatsPerBar ? info.beatsPerBar : 4), sink.put(2), sink.put(24), sink.put(8); // n/4
  }
  if (includeEvents) {
    for (uint32_t i = 0; i < count; i++) {
      const LoopEvent &e = events[i];
      if (channel && (e.status & 0x0F) != channel - 1) continue;
      putVlq(sink, e.tick - last);
      last = e.tick;
      sink.put(e.status);
      sink.put(e.data1);
      if (dataLength(e.status) == 2) sink.put(e.data2);
    }
  }
  uint32_t end = info.lengthTicks > last ? info.lengthTicks : last;
  putVlq(sink, end - last);
  sink.put(0xFF), sink.put(0x2F), sink.put(0x00);
}

static void writeTrack(const LoopEvent *events, uint32_t count, const Smf::Info &info, int channel, bool meta,
                       bool includeEvents, Smf::Sink &sink)
{
  Smf::Sink counter; // track lengths are known only after encoding: size it first
  writeTrackBody(events, count, info, channel, meta, includeEvents, counter);
  sink.put('M'), sink.put('T'), sink.put('r'), sink.put('k');
  put32(sink, (uint32_t)counter.length);
  writeTrackBody(events, count, info, channel, meta, includeEvents, sink);
}

size_t Smf::write(const LoopEvent *events, uint32_t count, const Info &info, bool type1, Sink &sink)
{
  size_t start = sink.length;
  bool channels[16] = {};
  int trackCount = 1;
  if (type1) {
    for (uint32_t i = 0; i < count; i++) channels[events[i].status & 0x0F] = true;
    for (bool used : channels) trackCount += used;
  }
  sink.put('M'), sink.put('T'), sink.put('h'), sink.put('d');
  put32(sink, 6);
  sink.put(0), sink.put(type1 ? 1 : 0);
  sink.put(trackCount >> 8), sink.put(trackCount);
  sink.put(kDivision >> 8), sink.put(kDivision & 0xFF);
  if (!type1) {
    writeTrack(events, count, info, 0, true, true, sink);
  } else {
    writeTrack(events, count, info, 0, true, false, sink); // tempo track
    for (int ch = 0; ch < 16; ch++) {
      if (channels[ch]) writeTrack(events, count, info, ch + 1, false, true, sink);
    }
  }
  return sink.length - start;
}

// ---------------------------------------------------------------------------- reading

namespace
{
struct Reader
{
  const uint8_t *p, *end;
  bool ok = true;

  bool need(size_t n)
  {
    if ((size_t)(end - p) < n) ok = false;
    return ok;
  }
  uint8_t u8() { return need(1) ? *p++ : 0; }
  uint32_t u32()
  {
    if (!need(4)) return 0;
    uint32_t v = (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
    p += 4;
    return v;
  }
  uint32_t vlq()
  {
    uint32_t v = 0;
    for (int i = 0; i < 4; i++) {
      uint8_t b = u8();
      v = (v << 7) | (b & 0x7F);
      if (!(b & 0x80)) return v;
    }
    ok = false;
    return v;
  }
};
} // namespace

bool Smf::read(const uint8_t *data, size_t length, LoopEvent *events, uint32_t capacity, uint32_t &count,
               Info &info)
{
  count = 0;
  Reader r{data, data + length};
  if (!r.need(14) || memcmp(r.p, "MThd", 4) != 0) return false;
  r.p += 4;
  uint32_t headerLength = r.u32();
  const uint8_t *afterHeader = r.p + headerLength;
  r.u8(), r.u8(); // format: 0 and 1 are read the same way (tracks merged)
  uint16_t tracks = (uint16_t)(r.u8() << 8);
  tracks |= r.u8();
  uint16_t division = (uint16_t)(r.u8() << 8);
  division |= r.u8();
  if (!r.ok || (division & 0x8000) || !division) return false; // SMPTE time isn't supported
  r.p = afterHeader;

  uint64_t longest = 0;
  bool tooMany = false;
  for (uint16_t t = 0; t < tracks && r.ok && r.need(8); t++) {
    bool isTrack = memcmp(r.p, "MTrk", 4) == 0;
    r.p += 4;
    uint32_t trackLength = r.u32();
    if (!r.need(trackLength)) return false;
    const uint8_t *trackEnd = r.p + trackLength;
    if (!isTrack) { // unknown chunk
      r.p = trackEnd;
      t--;
      continue;
    }
    Reader tr{r.p, trackEnd};
    uint64_t time = 0;
    uint8_t running = 0;
    while (tr.ok && tr.p < tr.end) {
      time += tr.vlq();
      uint8_t b = tr.u8();
      if (b == 0xFF) { // meta
        uint8_t type = tr.u8();
        uint32_t len = tr.vlq();
        if (!tr.need(len)) break;
        if (type == 0x51 && len == 3) {
          uint32_t us = (uint32_t)tr.p[0] << 16 | (uint32_t)tr.p[1] << 8 | tr.p[2];
          if (us) info.bpm = 60e6f / us;
        } else if (type == 0x58 && len >= 1 && tr.p[0]) {
          info.beatsPerBar = tr.p[0];
        }
        tr.p += len;
        if (type == 0x2F) break;
        continue;
      }
      if (b == 0xF0 || b == 0xF7) { // SysEx: skipped
        uint32_t len = tr.vlq();
        if (!tr.need(len)) break;
        tr.p += len;
        running = 0;
        continue;
      }
      uint8_t status, first;
      if (b & 0x80) {
        status = b;
        first = tr.u8();
        running = b;
      } else {
        if (!running) break; // data without status: corrupt
        status = running;
        first = b;
      }
      uint8_t second = dataLength(status) == 2 ? tr.u8() : 0;
      if (count < capacity) {
        events[count++] = {(uint32_t)(time * LoopBuffer::kPpqn / division), status, (uint8_t)(first & 0x7F),
                           (uint8_t)(second & 0x7F), 0};
      } else {
        tooMany = true;
      }
    }
    if (time > longest) longest = time;
    r.p = trackEnd;
  }
  if (!r.ok || tooMany) return false;
  std::stable_sort(events, events + count, [](const LoopEvent &a, const LoopEvent &b) { return a.tick < b.tick; });
  uint32_t lengthTicks = (uint32_t)(longest * LoopBuffer::kPpqn / division);
  uint32_t bar = LoopBuffer::kPpqn * (info.beatsPerBar ? info.beatsPerBar : 4);
  if (count && events[count - 1].tick >= lengthTicks) lengthTicks = events[count - 1].tick + 1;
  info.lengthTicks = ((lengthTicks + bar - 1) / bar) * bar;
  if (!info.lengthTicks) info.lengthTicks = bar;
  return true;
}
