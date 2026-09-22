# Define variables that may change between projects
FILENAME = kmmain
PORT = /dev/ttyUSB2
DEVICE = attiny1616
PROGRAMMER = serialupdi

# Clock speed must be provided for delays to be timed properly.
# This is the speed AFTER the prescaler that clockInit() sets, not the
# oscillator: the 20MHz oscillator is divided by two. U1 runs on +5V, so 20MHz
# would be in spec, but nothing here needs it and the SPI and I2C dividers are
# tuned to 10.
CLOCK = 10000000

# Baud rate for programming
BAUD = 115200

# Every .cpp in this directory is part of the firmware
SOURCES = $(wildcard *.cpp)
OBJECTS = $(SOURCES:.cpp=.o)

# Which screen main() runs. Override on the command line: make DIAG_MODE=2
#   0 = the real text screen
#   1 = fill the panel with cycling colours, through the normal path
#   2 = pin wiggle, no SPI at all, for checking the wiring end to end
#   3 = read the controller ID back and report it on PB5
#   4 = flood the raw frame memory, ignoring panel size and offsets
DIAG_MODE ?= 0

# SPI clock divider for the display: 4, 16, 64 or 128 (2.5MHz, 625kHz, 156kHz,
# 78kHz at 10MHz). Override on the command line to test a long cable:
#   make LCD_SPI_DIV=128 upload
# The default lives in lcd_config.h.
LCD_SPI_DIV ?=
ifneq ($(LCD_SPI_DIV),)
SPI_DIV_FLAG = -DLCD_SPI_DIV=$(LCD_SPI_DIV)
endif

# Any extra -D flags for one-off experiments: make EXTRA=-DLOOP_DELAY_MS=1000
EXTRA ?=

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
	-ffunction-sections -fdata-sections -flto -DDIAG_MODE=$(DIAG_MODE) $(SPI_DIV_FLAG) $(EXTRA)

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
# Everything also depends on the Makefile and on the recorded DIAG_MODE and
# LCD_SPI_DIV, so that changing any of them actually rebuilds. Without the
# stamp file, switching with "make DIAG_MODE=2" would change no file make can
# see, and it would happily reflash the previous mode's binary.
$(OBJECTS): Makefile diagmode.stamp

.PHONY: force
diagmode.stamp: force
	@echo "$(DIAG_MODE) $(LCD_SPI_DIV) $(EXTRA)" | cmp -s - $@ || echo "$(DIAG_MODE) $(LCD_SPI_DIV) $(EXTRA)" > $@
kmmain.o: lcd.h font.h font5x7.h font8x16.h lcd_config.h pcf8574.h signals.h
lcd.o: lcd.h font.h font5x7.h font8x16.h lcd_config.h
font5x7.o: font.h font5x7.h
font8x16.o: font.h font8x16.h
pcf8574.o: pcf8574.h
signals.o: signals.h pcf8574.h

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
