/*
 * MIT License
 *
 * Copyright (c) 2019 gdsports625@gmail.com
 * Copyright (c) 2024 zapthegoose@protonmail.com
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
#include <M5Unified.h>

#include "src/analyser_page.h"
#include "src/clock_analyser.h"
#include "src/clock_engine.h"
#include "src/clock_page.h"
#include "src/diag_page.h"
#include "src/diagnostics.h"
#include "src/din_midi.h"
#include "src/input_observers.h"
#include "src/leds.h"
#include "src/midi_outputs.h"
#include "src/midi_task.h"
#include "src/monitor.h"
#include "src/note_tracker.h"
#include "src/panic.h"
#include "src/router.h"
#include "src/router_page.h"
#include "src/sam_synth.h"
#include "src/speaker_synth.h"
#include "src/spi_bus.h"
#include "src/synth_page.h"
#include "src/ui.h"
#include "src/usb_midi.h"

// MIDI side: used only by the MIDI task (core 0) once it has started
UsbMidi usbMidi;
DinMidi dinMidi;
NoteTracker noteTracker;
MidiOutputs outputs;
Router router;           // route() in the MIDI task; settings edited by the UI
ClockEngine clockEngine; // runs in the MIDI task; settings and transport from the UI
Panic panic;             // runs in the MIDI task; requested from the UI
Stats stats;             // written by the MIDI task, read by the Diagnostics page
SamSynth samSynth;       // runs in the MIDI task; settings from the Synth page

// UI side: Arduino loop() (core 1)
Leds leds;
Ui ui;
Monitor monitor;
RouterPage routerPage(router, leds, clockEngine);
SpeakerSynth speakerSynth;
SynthPage synthPage(samSynth, speakerSynth);
ClockPage clockPage(clockEngine, leds);
ClockAnalyser clockAnalyser; // stage 9: fed by an input observer in the MIDI task
AnalyserPage analyserPage(clockAnalyser, leds);
DiagPage diagPage(stats, panic, outputs, dinMidi, usbMidi, leds);

/*! Every complete message from any input arrives here, in the MIDI task */
void onMidi(const MidiMessage &msg)
{
  // Interceptors (e.g. a pad controller playing the drum machine) may consume a message
  bool forwarded = !InputHooks::intercept(msg) && router.route(msg);
  InputHooks::observe(msg, forwarded);
  stats.observe(msg, forwarded, millis());
  panic.observe(msg);
  MidiTask::events().push(msg, forwarded);
}

/*! Every MIDI task loop, after polling the inputs */
void midiTaskLoop(uint32_t nowMillis)
{
  router.applyPending(); // settings changed on the UI side, even without MIDI traffic
  panic.run(nowMillis);
  samSynth.run(nowMillis);
  stats.run();
}

void setup()
{
  M5.begin();
  Serial.setTxBufferSize(4096); // the monitor prints every line; it must never block the UI
  Serial.begin(115200);
  SpiBus::begin();
  uint32_t splashStart = millis();
  Ui::splash("starting");

  // Unit MIDI on Port C (blue Grove port of the M5GO Bottom2), pins as M5Unified names them
  // (RX GPIO 13, TX GPIO 14), as in M5Stack's Unit MIDI example
  dinMidi.begin(Serial2, M5.getPin(m5::pin_name_t::port_c_rxd), M5.getPin(m5::pin_name_t::port_c_txd), onMidi);
  outputs.begin(dinMidi, usbMidi, noteTracker);
  clockEngine.begin(outputs);
  router.begin(outputs, clockEngine);
  samSynth.begin(outputs);
  ClockSource oldClock;
  if (router.migratedClock(oldClock)) { // settings from before the clock engine existed
    static const ClockMode kModes[] = {ClockMode::FollowDin, ClockMode::FollowUsb, ClockMode::FollowDin,
                                       ClockMode::Internal};
    clockEngine.migrateFromRouter(kModes[(int)oldClock], oldClock == ClockSource::Off);
  }

  leds.begin();
  leds.setLevel(router.config().ledLevel);
  speakerSynth.begin();

  Ui::splash("USB host");
  bool usbOk;
  {
    SpiLock bus("usb init");
    usbOk = usbMidi.begin(onMidi);
  }
  if (!usbOk) {
    Serial.println("USB host init failed");
    Ui::splash("USB host failed: check DIP switches");
    while (true) delay(1000);
  }

  // Keep the start-up screen up for at least 3 s, then the pages take over
  while (millis() - splashStart < 3000) delay(10);
  ui.begin(clockEngine);
  monitor.begin();
  ui.addPage(monitor);
  ui.addPage(routerPage);
  ui.addPage(synthPage);
  ui.addPage(clockPage);
  ui.addPage(analyserPage);
  ui.addPage(diagPage);

  InputHooks::addObserver(ClockAnalyser::observer, &clockAnalyser);
  panic.begin(outputs, noteTracker, usbMidi, clockEngine);
  MidiTask::setLoopHook(midiTaskLoop);
  MidiTask::start(usbMidi, dinMidi, clockEngine, outputs);
  Serial.printf("OxiMate running (DIN on GPIO RX %d / TX %d)\n", dinMidi.rxPin(), dinMidi.txPin());
}

/*! Serial status line every 2 s while DIN is being brought up */
void logDinStatus()
{
  static uint32_t last = 0;
  if (millis() - last < 2000 || clockAnalyser.csv()) return; // CSV streaming owns the serial port
  last = millis();
  const DinMidi::Counters &c = dinMidi.counters();
  Serial.printf("DIN rx %lu bytes, tx %lu bytes, dropped %lu | USB %s, gap max %lu ms | bus held max %lu ms (%s)\n",
                (unsigned long)c.rxBytes, (unsigned long)c.txBytes, (unsigned long)c.txDropped,
                usbMidi.connected() ? "connected" : "--", (unsigned long)(usbMidi.counters().maxGapMicros / 1000),
                (unsigned long)(SpiBus::maxHoldMicros() / 1000), SpiBus::maxHolder());
  // Maxima per 2 s window (the Diag page shows the same window)
  usbMidi.resetGap();
  SpiBus::resetStats();
}

/*! Built-in speaker output (stage 6): plays what the synth chip would play, from the routed
 *  input events. Layers mode: the layer channels (drum layer as drums); All mode: every channel
 *  (channel 10 as drums). */
void feedSpeaker(const MidiMessage &msg, bool forwarded)
{
  const SynthConfig &c = samSynth.config();
  static uint8_t volume = 0;
  if (volume != c.masterVolume) {
    volume = c.masterVolume;
    speakerSynth.setVolume(volume);
  }
  if (!c.speaker || !forwarded || !msg.isChannel()) return;
  uint8_t type = msg.status() & 0xF0;
  if (type != 0x90 && type != 0x80) return;
  uint8_t channel = msg.channel();
  bool play = false, drums = false;
  if (c.listen == SynthListen::All) {
    play = true;
    drums = channel == 10;
  } else if (c.listen == SynthListen::Layers) {
    for (int layer = 0; layer < SamSynth::kLayers; layer++) {
      if (c.layers[layer].channel == channel) {
        play = true;
        drums = drums || layer == SamSynth::kDrumLayer;
      }
    }
  }
  if (!play) return;
  if (type == 0x90 && msg.bytes[2] > 0) {
    speakerSynth.noteOn(msg.bytes[1], msg.bytes[2], drums);
  } else if (!drums) {
    speakerSynth.noteOff(msg.bytes[1]);
  }
}

/*! Power button: short press = panic levels 1+2, ~1.5 s hold = level 3 (any page).
 *  Every press shows PANIC in the header and flashes the LED bar red; a long panic also
 *  shows its progress there. */
void handlePanic()
{
  static uint32_t requestedAt = 0;
  uint8_t level = 0;
  if (M5.BtnPWR.wasClicked()) {
    level = 2;
  } else if (M5.BtnPWR.wasHold()) {
    level = 3;
  }
  if (level) {
    panic.request(level);
    requestedAt = millis();
    ui.flash(level >= 3 ? "PANIC ALL" : "PANIC", TFT_RED, 1500);
    Serial.printf("Panic level %u requested\n", level);
  }

  // Red while the panic runs, and at least 400 ms so a quick one is still visible
  Panic::Summary state = panic.summary();
  bool recent = requestedAt && millis() - requestedAt < 400;
  if (state.active || recent) {
    RGBColor bar[Leds::kCount];
    int lit = state.active ? (state.progress * Leds::kCount + 99) / 100 : Leds::kCount;
    for (int i = 0; i < Leds::kCount; i++) bar[i] = i < lit ? RGBColor(255, 0, 0) : RGBColor(32, 0, 0);
    leds.setOverlay(bar);
  } else if (requestedAt) {
    requestedAt = 0;
    leds.setOverlay(nullptr);
  }
}

/*! Logs which part of a slow UI loop took the time (at most once a second) */
void logSlowLoop(uint32_t start, uint32_t events, uint32_t input, uint32_t drawn, uint32_t end)
{
  static uint32_t lastLog = 0;
  if (end - start < 150000 || millis() - lastLog < 1000) return; // a sliced redraw takes ~70 ms
  lastLog = millis();
  Serial.printf("Slow UI loop %lu ms: events %lu, input %lu, draw %lu, rest %lu ms\n",
                (unsigned long)((end - start) / 1000), (unsigned long)((events - start) / 1000),
                (unsigned long)((input - events) / 1000), (unsigned long)((drawn - input) / 1000),
                (unsigned long)((end - drawn) / 1000));
}

void loop()
{
  uint32_t loopStart = micros();
  MidiEvent event;
  while (MidiTask::events().pop(event)) {
    MidiMessage msg = event.message();
    leds.activity(msg);
    feedSpeaker(msg, event.forwarded);
    ui.onMidi(msg, event.forwarded);
    monitor.log(msg, event.forwarded, event.length);
  }

  uint32_t eventsDone = micros();
  M5.update();
  handlePanic();
  ui.setUsbDevice(usbMidi.connected(), usbMidi.vendorId(), usbMidi.productId());
  uint32_t inputDone = micros();
  ui.poll();
  uint32_t drawDone = micros();
  clockPage.pollImu();
  leds.update();
  router.poll();
  clockEngine.poll();
  samSynth.poll();
  speakerSynth.update();
  diagPage.eventsDropped(MidiTask::events().dropped());
  uint32_t loopEnd = micros();
  diagPage.loopTime(loopEnd - loopStart);
  logSlowLoop(loopStart, eventsDone, inputDone, drawDone, loopEnd);
  logDinStatus();
  analyserPage.pollCsv();
  monitor.setSerialEcho(!clockAnalyser.csv());
  delay(1); // let the idle task run; the UI doesn't need more than ~1 kHz
}
