# Created By: zapTheGoose
.PHONY: all build send test

PORT ?= /dev/cu.usbserial-58620052741

all: build send

libraries/USB_Host_Shield_2.0/.patched:
	git clone https://github.com/felis/USB_Host_Shield_2.0.git libraries/USB_Host_Shield_2.0
	cd libraries/USB_Host_Shield_2.0 && git checkout ef5b046
	patch -s -p0 < USB_Host_Shield_2.0.patch
	touch libraries/USB_Host_Shield_2.0/.patched

build: libraries/USB_Host_Shield_2.0/.patched
	time arduino-cli compile \
		--fqbn esp32:esp32:m5stack_core2 \
		--libraries libraries

send:
	arduino-cli upload \
		--port $(PORT) \
		--fqbn esp32:esp32:m5stack_core2


# Host unit tests (no hardware needed)
TEST_SOURCES = test/test_main.cpp src/din_midi.cpp src/usb_midi.cpp src/router.cpp \
	src/midi_outputs.cpp src/note_tracker.cpp src/midi_task.cpp
test:
	mkdir -p build
	c++ -std=c++17 -Wall -Wno-unused-private-field -Itest/stubs -Isrc $(TEST_SOURCES) -o build/tests
	./build/tests
