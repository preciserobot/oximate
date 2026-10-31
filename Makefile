# Created By: zapTheGoose
.PHONY: all build send

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

