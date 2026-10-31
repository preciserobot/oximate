# OxiMate

Oxi One Remote for Dirtywave M8

## What shall it do

1. Show MIDI Messages on diplay (does it already)
2. Act as a USB MIDI host and send to attached midi port.
3. Remote control the Oxi One? Does it override Oxi behaviour?
4. Build Ableton Push 2 Interface

## What is currently does

Makes the Core2 remote control the Oxi One sequencer to make it usable as a control surface for the Dirtywave M8.

1. Set the Oxi One to Remote control mode
2. Update Display
3. Filter any messages from Oxi user inputs and route to specific MIDI commands.

## Hardware

* M5Stack core2 (ESP32, battery, display, etc.)
* M5Stack USB host module v1.2 (MAX3421E)
* M5Stack GO Bottom2 (optional)
* M%Stack MIDI M5-SAM2695

* USB MIDI keyboard or other USB-MIDI compliant device.

## Libraries

* USB Host Shield 2.0 <https://github.com/felis/USB_Host_Shield_2.0>
* M5-SAM2695 <https://github.com/m5stack/M5-SAM2695>
* M5Unified
* M5GFX

## Setup

> The Core2 uses different GPIO mappings for the SPI interface.

### USB Host Shield 2.0-core2patch

One must use the patched version of the `USB Host Shield 2.0` library
with the correct pin mappings (1.7.0-core2patch).

The pathched library is included in the `libraries` folder.

### M5Stack USB Module v1.2

Set the DIP switches to accroding to the mapping below.

SPI  | GPIO | Comment
-----|------|--------
MOSI | 23   |
MISO | 38   |
SCK  | 18   |
SS   | 19   | Set this on the USB board.
INT  | 35   | Set this on the USB board.

> Do not use 33 for `SS`, its used by the I2C bus that binds IMU and Microphone.

### Compile and install

The `Makefile` has target to compile and send to the Core2.
