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

#include "src/clock_tracker.h"
#include "src/din_midi.h"
#include "src/leds.h"
#include "src/midi_outputs.h"
#include "src/midi_task.h"
#include "src/monitor.h"
#include "src/note_tracker.h"
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
Router router; // route() in the MIDI task; settings edited by the UI

// UI side: Arduino loop() (core 1)
ClockTracker clockTracker;
Leds leds;
Ui ui;
Monitor monitor;
RouterPage routerPage(router, leds);

/*! Every complete message from any input arrives here, in the MIDI task */
void onMidi(const MidiMessage &msg)
{
  bool forwarded = router.route(msg);
  MidiTask::events().push(msg, forwarded);
}

void setup()
{
  M5.begin();
  Serial.begin(115200);
  SpiBus::begin();

  ui.begin(clockTracker);
  monitor.begin();
  ui.addPage(monitor);
  ui.addPage(routerPage);

  // Unit MIDI on Port C (blue Grove port of the M5GO Bottom2)
  dinMidi.begin(Serial2, M5.getPin(m5::pin_name_t::port_c_rxd), M5.getPin(m5::pin_name_t::port_c_txd), onMidi);
  outputs.begin(dinMidi, usbMidi, noteTracker);
  router.begin(outputs);

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

  MidiTask::start(usbMidi, dinMidi);
  Serial.println("OxiMate running");
}

void loop()
{
  MidiEvent event;
  while (MidiTask::events().pop(event)) {
    MidiMessage msg = event.message();
    clockTracker.track(msg);
    leds.activity(msg);
    monitor.log(msg, event.forwarded, event.length);
  }

  M5.update();
  ui.setUsbDevice(usbMidi.connected(), usbMidi.vendorId(), usbMidi.productId());
  ui.poll();
  leds.update();
  router.poll();
  delay(1); // let the idle task run; the UI doesn't need more than ~1 kHz
}
