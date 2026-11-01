#pragma once

#include "midi_outputs.h"
#include "sync.h"

/*! What the SAM2695 plays */
enum class SynthListen : uint8_t
{
  All = 0,    ///< every channel, GM defaults (power-up behaviour)
  Layers = 1, ///< only the configured layers
  Off = 2,    ///< silent
};

/*! Effects vs. polyphony (NRPN 375Fh) */
enum class SynthVoices : uint8_t
{
  Full = 0, ///< reverb + chorus, ~48 voices
  Lite = 1, ///< chorus only, ~61 voices
  Dry = 2,  ///< no effects, 64 voices
};

struct SynthLayer
{
  uint8_t channel; ///< 0 = off, 1-16
  uint8_t bank;    ///< 0 = GM, 127 = MT-32 variation bank (melodic layers)
  uint8_t program; ///< 0-127 (drum layer: kit program)
  uint8_t volume;
  uint8_t reverb; ///< reverb send
};

/*! Synth settings, persisted in NVS (key "synth"). Bump kVersion when the layout changes. */
struct SynthConfig
{
  static constexpr uint8_t kVersion = 1;

  uint8_t version = kVersion;
  SynthListen listen = SynthListen::All;
  uint8_t configChannel = 16; ///< settings are sent here; no external gear should use it
  bool bypass = false;        ///< the Unit MIDI switch is on Bypass (only affects warnings)
  /// Layers 0-3 are melodic (SAM parts 1-4), layer 4 is the drum layer (part 0)
  SynthLayer layers[5] = {{1, 0, 0, 100, 40}, {0, 0, 48, 90, 60}, {0, 0, 80, 90, 20}, {0, 0, 38, 100, 20},
                          {10, 0, 0, 110, 30}};
  uint8_t masterVolume = 127;
  uint8_t reverbType = 4; ///< 0 Room1 .. 4 Hall2 .. 7 Pan delay
  uint8_t reverbLevel = 64;
  uint8_t chorusLevel = 0;
  SynthVoices voices = SynthVoices::Full;
};

/*! The single way OxiMate talks to the SAM2695 synth chip (stage 6).
 *
 *  The Core2's output drives both the SAM2695 and DIN OUT, so anything sent to the synth also
 *  reaches external gear. To keep that to a minimum, the chip's parts are pointed at the
 *  channels they should play (GS "part assign" SysEx, a muted part = OFF), and all settings are
 *  sent on a reserved config channel while the part is temporarily assigned to it.
 *
 *  UI side: config()/changed()/poll(), resend(), testLayer(), preview().
 *  MIDI task side: run(); the outputs listener detects resets that pass through to DIN OUT and
 *  re-applies the state. */
class SamSynth
{
public:
  static constexpr uint8_t kOwner = 6; ///< Origin::internal owner id
  static constexpr int kLayers = 5;
  static constexpr int kDrumLayer = 4;
  static constexpr int kParts = 16;
  static constexpr uint32_t kAuditionMillis = 700;
  static constexpr uint32_t kRestoreDelayMillis = 100; ///< the chip needs ~50 ms after a reset

  static int partOf(int layer) { return layer == kDrumLayer ? 0 : layer + 1; }
  /*! GS default channel (1-16) of a part: part 0 = 10, 1-9 = 1-9, 10-15 = 11-16 */
  static uint8_t defaultChannel(int part) { return part == 0 ? 10 : (part <= 9 ? part : part + 1); }
  static uint8_t voicesValue(SynthVoices voices);

  /*! A Roland GS data set (device 00h, checksum included) of one value; returns 11 */
  static uint16_t gsMessage(uint8_t *out, uint8_t address1, uint8_t address2, uint8_t address3, uint8_t value);
  /*! True for System Reset and GM/GS reset messages (they reset the SAM2695 too) */
  static bool isReset(const MidiMessage &msg);

  void begin(MidiOutputs &outputs);

  // UI side
  SynthConfig &config() { return _edit; }
  void changed(); ///< apply config() in the MIDI task; saved after a short delay
  void poll();    ///< delayed save
  void resend();  ///< re-send the complete state
  void testLayer(int layer);
  void preview(int layer, uint8_t bank, uint8_t program); ///< audition a sound on a layer's part

  // MIDI task side
  void run(uint32_t nowMillis);

private:
  struct Audition
  {
    bool active;
    int part;
    int returnLayer; // layer whose settings are re-applied afterwards, -1 = none
    bool drums;
    uint32_t until;
    uint16_t muted; // parts muted while the audition plays on the config channel
  };

  static void onSent(MidiOutput out, const MidiMessage &msg, Origin origin, void *ctx);

  void send(const uint8_t *bytes, uint16_t length);
  void controlChange(uint8_t channel, uint8_t controller, uint8_t value);
  void nrpn(uint8_t msb, uint8_t lsb, uint8_t value);
  void gs(uint8_t address1, uint8_t address2, uint8_t address3, uint8_t value);
  void assign(int part, uint8_t channel); ///< 0 = OFF
  void configureOnConfigChannel(int part, const SynthLayer &layer, bool drums);
  uint8_t targetChannel(int layer) const;
  uint8_t partChannel(int part) const;
  uint16_t muteConfigListeners(int exceptPart);
  void unmuteParts(uint16_t muted);
  void applyLayer(int layer);
  void applyGlobals();
  void restore();
  void startAudition(int layer, uint8_t bank, uint8_t program, uint32_t nowMillis);
  void endAudition();
  void load();
  void save();

  MidiOutputs *_outputs = nullptr;

  // UI side
  SynthConfig _edit;
  uint32_t _changedMillis = 0;
  bool _dirty = false;

  // Handoff (guarded by _lock)
  CriticalSection _lock;
  SynthConfig _pending;
  bool _pendingSet = false;
  bool _restoreRequested = false;
  int8_t _testRequested = -1;
  int8_t _previewLayer = -1;
  uint8_t _previewBank = 0, _previewProgram = 0;

  // MIDI task side
  SynthConfig _active;
  uint32_t _restoreAt = 0; ///< millis, 0 = none scheduled
  bool _restoreDue = false;
  Audition _audition = {};
};
