#pragma once

#include <stdint.h>

/*! General MIDI percussion names (channel 10, notes 35-81), short enough for the Drums page */
namespace DrumKits
{
static constexpr uint8_t kFirstNote = 35;
static constexpr uint8_t kLastNote = 81;

static const char *const kNoteNames[kLastNote - kFirstNote + 1] = {
    "Acoustic Kick", "Kick 1",        "Side Stick",   "Snare 1",      "Hand Clap",    "Snare 2",
    "Low Floor Tom", "Closed Hat",    "Hi Floor Tom", "Pedal Hat",    "Low Tom",      "Open Hat",
    "Low-Mid Tom",   "Hi-Mid Tom",    "Crash 1",      "High Tom",     "Ride 1",       "Chinese Cym",
    "Ride Bell",     "Tambourine",    "Splash",       "Cowbell",      "Crash 2",      "Vibraslap",
    "Ride 2",        "Hi Bongo",      "Low Bongo",    "Mute Hi Conga", "Open Hi Conga", "Low Conga",
    "Hi Timbale",    "Low Timbale",   "Hi Agogo",     "Low Agogo",    "Cabasa",       "Maracas",
    "Short Whistle", "Long Whistle",  "Short Guiro",  "Long Guiro",   "Claves",       "Hi Woodblock",
    "Low Woodblock", "Mute Cuica",    "Open Cuica",   "Mute Triangle", "Open Triangle",
};

inline const char *noteName(uint8_t note)
{
  return note >= kFirstNote && note <= kLastNote ? kNoteNames[note - kFirstNote] : "(no GM sound)";
}
} // namespace DrumKits
