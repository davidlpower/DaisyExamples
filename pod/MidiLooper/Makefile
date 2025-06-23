# Project Name
TARGET = MidiLooper

USE_DAISYSP_LGPL = 1

# Sources
CPP_SOURCES = MidiLooper.cpp

# Library Locations
LIBDAISY_DIR = ../../libDaisy/
DAISYSP_DIR = ../../DaisySP/

# Core location, and generic Makefile.
SYSTEM_FILES_DIR = $(LIBDAISY_DIR)/core
include $(SYSTEM_FILES_DIR)/Makefile

# -------------------------------------------------
# Flash full binary via USB DFU (built-in bootloader)
# -------------------------------------------------
my_program_dfu:
	dfu-util -a 0 -d 0483:df11 \
	        --dfuse-address 0x08000000:leave \
	        -D build/$(TARGET).bin