#include "step_voice.h"

#include "note_tracker.h"

void StepVoice::begin(MidiOutputs &outputs, ClockEngine &clock, uint8_t owner)
{
  _outputs = &outputs;
  _clock = &clock;
  _owner = owner;
}

void StepVoice::send(uint8_t outputs, const uint8_t *bytes, uint16_t length)
{
  MidiMessage msg = {MidiSource::Din, 0, bytes, length};
  if (outputs & ToDin) _outputs->send(MidiOutput::Din, msg, origin());
  if ((outputs & ToUsb) && _outputs->available(MidiOutput::Usb)) _outputs->send(MidiOutput::Usb, msg, origin());
}

void StepVoice::noteOff(uint8_t outputs, uint8_t channel, uint8_t note)
{
  uint8_t off[3] = {(uint8_t)(0x80 | ((channel - 1) & 0x0F)), note, 0};
  send(outputs, off, 3);
}

/*! The scheduler passes one 32-bit argument: outputs, channel and note packed together */
void StepVoice::noteOffCallback(void *ctx, uint32_t packed, uint64_t)
{
  static_cast<StepVoice *>(ctx)->noteOff(packed >> 16, (packed >> 8) & 0xFF, packed & 0xFF);
}

void StepVoice::note(uint8_t outputs, uint8_t channel, uint8_t note, uint8_t velocity, uint64_t timeUs,
                     uint32_t gateMicros)
{
  uint8_t on[3] = {(uint8_t)(0x90 | ((channel - 1) & 0x0F)), note, velocity};
  send(outputs, on, 3);
  if (!gateMicros) return;
  uint32_t packed = ((uint32_t)outputs << 16) | ((uint32_t)channel << 8) | note;
  if (!_clock->schedule(timeUs + gateMicros, noteOffCallback, this, packed)) {
    noteOff(outputs, channel, note); // scheduler full: end it now rather than never
  }
}

void StepVoice::controlChange(uint8_t outputs, uint8_t channel, uint8_t controller, uint8_t value)
{
  uint8_t cc[3] = {(uint8_t)(0xB0 | ((channel - 1) & 0x0F)), controller, value};
  send(outputs, cc, 3);
}

void StepVoice::releaseAll()
{
  Origin own = origin();
  _outputs->tracker().release(*_outputs, MidiOutput::Din, &own);
  _outputs->tracker().release(*_outputs, MidiOutput::Usb, &own);
}
