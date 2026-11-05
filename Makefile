# Created By: zapTheGoose
.PHONY: all build send test log

PORT ?= /dev/cu.usbserial-58620052741

all: build send

libraries/USB_Host_Shield_2.0/.patched:
	git clone https://github.com/felis/USB_Host_Shield_2.0.git libraries/USB_Host_Shield_2.0
	cd libraries/USB_Host_Shield_2.0 && git checkout ef5b046
	patch -s -p0 < USB_Host_Shield_2.0.patch
	touch libraries/USB_Host_Shield_2.0/.patched

# Stage 17: NimBLE-Arduino pinned here (a copy in the sketchbook may be older, or incomplete)
libraries/NimBLE-Arduino/.cloned:
	git clone --depth 1 --branch 2.5.0 https://github.com/h2zero/NimBLE-Arduino.git libraries/NimBLE-Arduino
	touch libraries/NimBLE-Arduino/.cloned

build: libraries/USB_Host_Shield_2.0/.patched libraries/NimBLE-Arduino/.cloned
	time arduino-cli compile \
		--fqbn esp32:esp32:m5stack_core2 \
		--libraries libraries

send:
	arduino-cli upload \
		--port $(PORT) \
		--fqbn esp32:esp32:m5stack_core2


# Host unit tests (no hardware needed)
TEST_SOURCES = $(wildcard test/test_*.cpp) src/din_midi.cpp src/usb_midi.cpp src/router.cpp \
	src/midi_outputs.cpp src/note_tracker.cpp src/midi_task.cpp src/clock_engine.cpp src/panic.cpp src/sam_synth.cpp
# Stages add their host-testable sources in test/sources_*.mk (TEST_SOURCES += ...)
-include $(wildcard test/sources_*.mk)
test:
	mkdir -p build
	c++ -std=c++17 -Wall -Wno-unused-private-field -Itest/stubs -Isrc $(TEST_SOURCES) -o build/tests
	./build/tests

# Reset the board and show its serial output for SECONDS (default 10)
SECONDS ?= 10
log:
	python3 -I tools/serial_log.py $(PORT) $(SECONDS)
