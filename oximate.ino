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

#include "src/din_midi.h"
#include "src/monitor.h"
#include "src/usb_midi.h"

/*
 * MIDI merge routing (see README "MIDI routing").
 * Every input is sent to every output except back to where it came from.
 */
constexpr bool kRouteUsbToDin = true; // USB device -> DIN OUT (+ SAM2695 synth)
constexpr bool kRouteDinToDin = true; // DIN IN -> DIN OUT (software thru, merged with USB)
constexpr bool kRouteDinToUsb = true; // DIN IN -> USB device

UsbMidi usbMidi;
DinMidi dinMidi;
Monitor monitor;

void onMidi(const MidiMessage &msg)
{
  switch (msg.source) {
    case MidiSource::Usb:
      if (kRouteUsbToDin) dinMidi.send(msg);
      break;
    case MidiSource::Din:
      if (kRouteDinToDin) dinMidi.send(msg);
      if (kRouteDinToUsb) usbMidi.send(msg);
      break;
  }
  monitor.log(msg);
}

void setup()
{
  M5.begin();
  Serial.begin(115200);
  monitor.begin();

  // Unit MIDI on Port C (blue Grove port of the M5GO Bottom2)
  dinMidi.begin(Serial2, M5.getPin(m5::pin_name_t::port_c_rxd), M5.getPin(m5::pin_name_t::port_c_txd), onMidi);

  if (!usbMidi.begin(onMidi)) {
    Serial.println("USB host init failed");
    M5.Display.setTextColor(TFT_RED, TFT_BLACK);
    M5.Display.drawString("USB host init failed", 0, 40);
    M5.Display.drawString("Check module + DIP switches", 0, 60);
    while (true) delay(1000);
  }
  Serial.println("OxiMate running");
}

void loop()
{
  usbMidi.poll();
  dinMidi.poll();

  M5.update();
  if (M5.BtnA.wasClicked()) monitor.clear();
  if (M5.BtnB.wasClicked()) monitor.togglePause();
  if (M5.BtnC.wasClicked()) monitor.toggleRealtime();

  monitor.setUsbDevice(usbMidi.connected(), usbMidi.vendorId(), usbMidi.productId());
  monitor.draw();
}
