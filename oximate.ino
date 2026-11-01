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

#include "src/clock_engine.h"
#include "src/clock_page.h"
#include "src/diag_page.h"
#include "src/diagnostics.h"
#include "src/din_midi.h"
#include "src/leds.h"
#include "src/midi_outputs.h"
#include "src/midi_task.h"
#include "src/monitor.h"
#include "src/note_tracker.h"
#include "src/panic.h"
#include "src/router.h"
#include "src/router_page.h"
#include "src/spi_bus.h"
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

// UI side: Arduino loop() (core 1)
Leds leds;
Ui ui;
Monitor monitor;
RouterPage routerPage(router, leds, clockEngine);
ClockPage clockPage(clockEngine, leds);
DiagPage diagPage(stats, panic, outputs, dinMidi, usbMidi, leds);

/*! Every complete message from any input arrives here, in the MIDI task */
void onMidi(const MidiMessage &msg)
{
  bool forwarded = router.route(msg);
  stats.observe(msg, forwarded, millis());
  panic.observe(msg);
  MidiTask::events().push(msg, forwarded);
}

/*! Every MIDI task loop, after polling the inputs */
void midiTaskLoop(uint32_t nowMillis)
{
  router.applyPending(); // settings changed on the UI side, even without MIDI traffic
  panic.run(nowMillis);
  stats.run();
}

void setup()
{
  M5.begin();
  Serial.begin(115200);
  SpiBus::begin();

  ui.begin(clockEngine);
  monitor.begin();
  ui.addPage(monitor);
  ui.addPage(routerPage);
  ui.addPage(clockPage);
  ui.addPage(diagPage);

  // Unit MIDI on Port C (blue Grove port of the M5GO Bottom2)
  dinMidi.begin(Serial2, M5.getPin(m5::pin_name_t::port_c_rxd), M5.getPin(m5::pin_name_t::port_c_txd), onMidi);
  outputs.begin(dinMidi, usbMidi, noteTracker);
  clockEngine.begin(outputs);
  router.begin(outputs, clockEngine);
  ClockSource oldClock;
  if (router.migratedClock(oldClock)) { // settings from before the clock engine existed
    static const ClockMode kModes[] = {ClockMode::FollowDin, ClockMode::FollowUsb, ClockMode::FollowDin,
                                       ClockMode::Internal};
    clockEngine.migrateFromRouter(kModes[(int)oldClock], oldClock == ClockSource::Off);
  }

  leds.begin();
  leds.setLevel(router.config().ledLevel);

  bool usbOk;
  {
    SpiLock bus;
    usbOk = usbMidi.begin(onMidi);
  }
  if (!usbOk) {
    Serial.println("USB host init failed");
    DisplayLock lock;
    M5.Display.setTextColor(TFT_RED, TFT_BLACK);
    M5.Display.drawString("USB host init failed", 0, 40);
    M5.Display.drawString("Check module + DIP switches", 0, 60);
    while (true) delay(1000);
  }

  panic.begin(outputs, noteTracker, usbMidi, clockEngine);
  MidiTask::setLoopHook(midiTaskLoop);
  MidiTask::start(usbMidi, dinMidi, clockEngine);
  Serial.println("OxiMate running");
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

void loop()
{
  uint32_t loopStart = micros();
  MidiEvent event;
  while (MidiTask::events().pop(event)) {
    MidiMessage msg = event.message();
    leds.activity(msg);
    monitor.log(msg, event.forwarded, event.length);
  }

  M5.update();
  handlePanic();
  ui.setUsbDevice(usbMidi.connected(), usbMidi.vendorId(), usbMidi.productId());
  ui.poll();
  clockPage.pollImu();
  leds.update();
  router.poll();
  clockEngine.poll();
  diagPage.eventsDropped(MidiTask::events().dropped());
  diagPage.loopTime(micros() - loopStart);
  delay(1); // let the idle task run; the UI doesn't need more than ~1 kHz
}
