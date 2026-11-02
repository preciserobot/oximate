#include "syx_file.h"

#include <stdio.h>
#include <string.h>

namespace Syx
{

int split(const uint8_t *data, size_t length, Message *out, int max, Stats &stats)
{
  stats = Stats();
  int stored = 0;
  bool inside = false;
  size_t start = 0;
  for (size_t i = 0; i < length; i++) {
    uint8_t b = data[i];
    if (b == 0xF0) {
      if (inside) stats.stray += i - start; // a new F0 ends an unfinished message
      inside = true;
      start = i;
    } else if (b == 0xF7 && inside) {
      inside = false;
      uint32_t size = i - start + 1;
      if (stored < max) out[stored++] = {(uint32_t)start, size};
      stats.messages++;
      stats.bytes += size;
    } else if (inside && (b & 0x80) && b < 0xF8) {
      stats.stray += i - start + 1; // another status byte ends the message unfinished
      inside = false;
    } else if (!inside) {
      stats.stray++;
    }
  }
  if (inside) {
    stats.truncated = true;
    stats.stray += length - start;
  }
  return stored;
}

struct Maker
{
  uint8_t id[3]; // 1-byte IDs use id[0] only
  const char *name;
  const char *slug;
};

static const Maker kOneByte[] = {
    {{0x01}, "Sequential", "sequential"}, {{0x04}, "Moog", "moog"},     {{0x06}, "Lexicon", "lexicon"},
    {{0x0F}, "Ensoniq", "ensoniq"},       {{0x18}, "E-mu", "emu"},      {{0x40}, "Kawai", "kawai"},
    {{0x41}, "Roland", "roland"},         {{0x42}, "Korg", "korg"},     {{0x43}, "Yamaha", "yamaha"},
    {{0x44}, "Casio", "casio"},           {{0x47}, "Akai", "akai"},     {{0x7D}, "Non-commercial", "noncommercial"},
    {{0x7E}, "Universal", "universal"},   {{0x7F}, "Universal", "universal"},
};

static const Maker kThreeByte[] = {
    {{0x00, 0x20, 0x3C}, "Elektron", "elektron"}, {{0x00, 0x21, 0x5B}, "Oxi", "oxi"},
    {{0x00, 0x20, 0x29}, "Novation", "novation"}, {{0x00, 0x20, 0x6B}, "Arturia", "arturia"},
    {{0x00, 0x20, 0x32}, "Behringer", "behringer"},
};

static const Maker *find(const uint8_t *message, size_t length)
{
  if (length < 2 || message[0] != 0xF0) return nullptr;
  if (message[1] == 0x00) {
    if (length < 4) return nullptr;
    for (const Maker &maker : kThreeByte) {
      if (maker.id[1] == message[2] && maker.id[2] == message[3]) return &maker;
    }
    return nullptr;
  }
  for (const Maker &maker : kOneByte) {
    if (maker.id[0] == message[1]) return &maker;
  }
  return nullptr;
}

const char *manufacturer(const uint8_t *message, size_t length)
{
  const Maker *maker = find(message, length);
  return maker ? maker->name : "Unknown";
}

const char *slug(const uint8_t *message, size_t length)
{
  const Maker *maker = find(message, length);
  return maker ? maker->slug : "sysex";
}

bool isBroadcast(const uint8_t *message, size_t length)
{
  return length >= 3 && message[0] == 0xF0 && (message[1] == 0x7E || message[1] == 0x7F) && message[2] == 0x7F;
}

void fileName(char *out, size_t size, const char *slugName, int year, int month, int day, int hour, int minute,
              int second, uint16_t counter)
{
  if (year >= 2024) {
    snprintf(out, size, "%s-%04d%02d%02d-%02d%02d%02d.syx", slugName, year, month, day, hour, minute, second);
  } else {
    snprintf(out, size, "%s-%04u.syx", slugName, (unsigned)counter);
  }
}

bool UsbPacker::push(uint8_t byte, uint8_t cable, uint8_t packet[4])
{
  if (byte == 0xF0) _count = 0; // a new message always starts a packet
  _bytes[_count++] = byte;
  uint8_t cin;
  if (byte == 0xF7) {
    cin = 4 + _count; // SysEx ends with 1, 2 or 3 bytes: CIN 5, 6, 7
  } else if (_count == 3) {
    cin = 4; // SysEx starts or continues
  } else {
    return false;
  }
  packet[0] = (uint8_t)(cable << 4) | cin;
  for (int i = 0; i < 3; i++) packet[1 + i] = i < _count ? _bytes[i] : 0;
  _count = 0;
  return true;
}

} // namespace Syx
