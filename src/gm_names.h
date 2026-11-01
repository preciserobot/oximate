#pragma once

#include <stdint.h>

/*! General MIDI instrument names (program 0-127), 16 families of 8, and the SAM2695 drum kits
 *  (datasheet section 4-3: "Prog n" there is 1-based, the program numbers here are 0-based) */
namespace GmNames
{
static const char *const kPrograms[128] = {
    "Acoustic Grand",  "Bright Acoustic", "Electric Grand",  "Honky-tonk",      "E.Piano 1",       "E.Piano 2",
    "Harpsichord",     "Clavi",           "Celesta",         "Glockenspiel",    "Music Box",       "Vibraphone",
    "Marimba",         "Xylophone",       "Tubular Bells",   "Dulcimer",        "Drawbar Organ",   "Perc. Organ",
    "Rock Organ",      "Church Organ",    "Reed Organ",      "Accordion",       "Harmonica",       "Tango Accordion",
    "Nylon Guitar",    "Steel Guitar",    "Jazz Guitar",     "Clean Guitar",    "Muted Guitar",    "Overdrive Gtr",
    "Distortion Gtr",  "Gtr Harmonics",   "Acoustic Bass",   "Finger Bass",     "Pick Bass",       "Fretless Bass",
    "Slap Bass 1",     "Slap Bass 2",     "Synth Bass 1",    "Synth Bass 2",    "Violin",          "Viola",
    "Cello",           "Contrabass",      "Tremolo Strings", "Pizzicato Str",   "Orch. Harp",      "Timpani",
    "Strings 1",       "Strings 2",       "Synth Strings 1", "Synth Strings 2", "Choir Aahs",      "Voice Oohs",
    "Synth Voice",     "Orchestra Hit",   "Trumpet",         "Trombone",        "Tuba",            "Muted Trumpet",
    "French Horn",     "Brass Section",   "Synth Brass 1",   "Synth Brass 2",   "Soprano Sax",     "Alto Sax",
    "Tenor Sax",       "Baritone Sax",    "Oboe",            "English Horn",    "Bassoon",         "Clarinet",
    "Piccolo",         "Flute",           "Recorder",        "Pan Flute",       "Blown Bottle",    "Shakuhachi",
    "Whistle",         "Ocarina",         "Lead 1 Square",   "Lead 2 Saw",      "Lead 3 Calliope", "Lead 4 Chiff",
    "Lead 5 Charang",  "Lead 6 Voice",    "Lead 7 Fifths",   "Lead 8 Bass+Ld",  "Pad 1 Fantasia",  "Pad 2 Warm",
    "Pad 3 Polysynth", "Pad 4 Choir",     "Pad 5 Bowed",     "Pad 6 Metallic",  "Pad 7 Halo",      "Pad 8 Sweep",
    "FX 1 Rain",       "FX 2 Soundtrack", "FX 3 Crystal",    "FX 4 Atmosphere", "FX 5 Brightness", "FX 6 Goblins",
    "FX 7 Echoes",     "FX 8 Sci-fi",     "Sitar",           "Banjo",           "Shamisen",        "Koto",
    "Kalimba",         "Bagpipe",         "Fiddle",          "Shanai",          "Tinkle Bell",     "Agogo",
    "Steel Drums",     "Woodblock",       "Taiko Drum",      "Melodic Tom",     "Synth Drum",      "Reverse Cymbal",
    "Gtr Fret Noise",  "Breath Noise",    "Seashore",        "Bird Tweet",      "Telephone",       "Helicopter",
    "Applause",        "Gunshot",
};

/*! Family labels for the browser, 8 programs each */
static const char *const kFamilies[16] = {"Pno", "Chr", "Org", "Gtr", "Bas", "Str", "Ens", "Brs",
                                          "Red", "Pip", "Led", "Pad", "SFX", "Eth", "Prc", "FX"};

struct Kit
{
  uint8_t program;
  const char *name;
};
/*! CM-64/32: the datasheet gives both Prog 127 and Prog 128; 127 (0-based) is used here and
 *  needs checking on the hardware */
static const Kit kKits[] = {
    {0, "Standard"}, {16, "Power"}, {40, "Brush"}, {48, "Orchestra"}, {127, "CM-64/32"},
};
static constexpr int kKitCount = sizeof(kKits) / sizeof(kKits[0]);

inline const char *kitName(uint8_t program)
{
  for (const Kit &kit : kKits) {
    if (kit.program == program) return kit.name;
  }
  return "Kit ?";
}
} // namespace GmNames
