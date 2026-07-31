# Define variables that may change between projects
FILENAME = kmmain
PORT = /dev/ttyUSB1
DEVICE = attiny1616
PROGRAMMER = serialupdi

# Clock speed must be provided for delays to be timed properly.
# This is the speed AFTER the prescaler that clockInit() sets, not the
# oscillator: the 20MHz oscillator is divided by two because the ATtiny1616 is
# only rated for 20MHz at 4.5V and up, and this board runs at 3.3V, where
# 10MHz is the limit.
CLOCK = 10000000

# Baud rate for programming
BAUD = 115200

# Every .cpp in this directory is part of the firmware
SOURCES = $(wildcard *.cpp)
OBJECTS = $(SOURCES:.cpp=.o)

# Which screen main() runs. Override on the command line: make DIAG_MODE=2
#   0 = the real text screen
#   1 = flood the whole frame memory with cycling colours
#   2 = pin wiggle, no SPI at all, for checking the wiring end to end
#   3 = read the controller ID back and report it on PB5
DIAG_MODE ?= 1

# Compile with options:
# - All warnings (-Wall)
# - Optimized for size (-0s)
# - Targeted to our mcu (-mmcu)
# - F_CPU defined as our clock speed (-DF_CPU)
# - No exceptions, RTTI or threadsafe statics: none of it works on an 1616 and
#   all of it drags in library code we have no room for
# - Sections per function plus --gc-sections, so unused driver code is dropped
COMPILE = avr-g++ -Wall -Os -std=c++17 -mmcu=$(DEVICE) -DF_CPU=$(CLOCK) \
	-fno-exceptions -fno-rtti -fno-threadsafe-statics \
	-ffunction-sections -fdata-sections -flto -DDIAG_MODE=$(DIAG_MODE)

LINK = $(COMPILE) -Wl,--gc-sections

#default: compile upload clean
default: compile

# Compile step
# file.cpp -> file.o -> file.elf -> file.hex
# Then print out the size of the binary: check "data + bss" stays well under
# the 2048 bytes of SRAM, and "text + data" under the 16384 bytes of flash
compile: $(FILENAME).hex

%.o: %.cpp
	$(COMPILE) -c $< -o $@

$(FILENAME).elf: $(OBJECTS)
	$(LINK) -o $@ $(OBJECTS)

$(FILENAME).hex: $(FILENAME).elf
	avr-objcopy -O ihex $(FILENAME).elf $(FILENAME).hex
	avr-size $(FILENAME).elf

# Header dependencies, kept by hand since there are only a few of them.
#
# Everything also depends on the Makefile and on the recorded DIAG_MODE, so
# that changing either actually rebuilds. Without the stamp file, switching
# with "make DIAG_MODE=2" would change no file make can see, and it would
# happily reflash the previous mode's binary.
$(OBJECTS): Makefile diagmode.stamp

.PHONY: force
diagmode.stamp: force
	@echo "$(DIAG_MODE)" | cmp -s - $@ || echo "$(DIAG_MODE)" > $@
kmmain.o: st7789.h font5x7.h lcd_config.h
st7789.o: st7789.h font5x7.h lcd_config.h
font5x7.o: font5x7.h

# Upload step
# Flash the .hex file onto the chip
upload:
	avrdude -v -p $(DEVICE) -c $(PROGRAMMER) -P $(PORT) -b $(BAUD) -U flash:w:$(FILENAME).hex:i

# Remove intermediate files
clean:
	rm -f $(OBJECTS)
	rm -f $(FILENAME).elf
	rm -f $(FILENAME).hex
	rm -f diagmode.stamp

# Fuses are non-volatile settings that can be set by avrdude and persist between resets
# Setting fuse2 to 0x7e enables 20Mhz clock
# See page 34 of https://ww1.microchip.com/downloads/en/DeviceDoc/ATtiny1614-16-17-DataSheet-DS40002204A.pdf
fuse:
	avrdude -v -p $(DEVICE) -c $(PROGRAMMER) -P $(PORT) -b $(BAUD) -U fuse2:w:0x7e:m
