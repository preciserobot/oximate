# OxiMate

Oxi One Remote for Dirtywave M8

## What shall it do

1. Show MIDI Messages on diplay (does it already)
2. Act as a USB MIDI host and send to attached midi port.
3. Remote control the Oxi One? Does it override Oxi behaviour?
3. Build Abletopn Push 2 Interface

## What is currently does

Makes the Core2 remote control the Oxi One sequencer to make it usable as a control surface for the Dirtywave M8.

1. Set the Oxi One to Remote control mode
2. Update Display
3. Filter any messages from Oxi user inputs and route to specific MIDI commands.

## M5Stack Core + USB host module

The M5Stack USB module provides the USB host interface for the printer. The USB
module is based on the MAX3421E USB chip which is supported by the USB Host
Shield 2.0 library.

### Hardware

* M5Stack core (ESP32, battery, display, etc.) https://m5stack.com/collections/m5-core/products/grey-development-core
* M5Stack USB host module (MAX3421E) https://m5stack.com/collections/m5-module/products/usb-module
* USB MIDI keyboard
* USB cable

### Libraries

* USB Host Shield 2.0 https://github.com/felis/USB_Host_Shield_2.0
* Highly recommended: PR #473
* Highly recommended if using USB hubs: PR #414

## Arduino upload failure

I found installing a 1 uF capacitor between GND and RST helps a lot. See
the link for more discussion.

http://community.m5stack.com/topic/55/simple-fix-when-upload-fails

## Battery Power On Failure

When running on battery, the M5Stack does not turn on when the USB host module
is installed. The M5Stack works fine running on battery but once it is turned
off, it can only be turned on by plugging in power via the USB jack.

The M5Stack power on works correctly when the USB host module is removed.
