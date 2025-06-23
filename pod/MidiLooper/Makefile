# Project Names
BASE_TARGET = MidiLooper
TARGET := $(BASE_TARGET)
ifeq ($(PRO),1)
	TARGET := $(BASE_TARGET)Pro
endif

USE_DAISYSP_LGPL = 1

# Sources
CPP_SOURCES = $(BASE_TARGET).cpp

# Library Locations
LIBDAISY_DIR = ../../libDaisy/
DAISYSP_DIR = ../../DaisySP/

# Core location, and generic Makefile.
SYSTEM_FILES_DIR = $(LIBDAISY_DIR)/core
include $(SYSTEM_FILES_DIR)/Makefile

# -------------------------------------------------
# Flash full binary via USB DFU (built-in bootloader)
# Usage: make my_program_dfu or make pro_program_dfu PRO=1
# -------------------------------------------------
my_program_dfu:
	dfu-util -a 0 -d 0483:df11 \
	        --dfuse-address 0x08000000:leave \
	        -D build/$(BASE_TARGET).bin

pro_program_dfu:
	dfu-util -a 0 -d 0483:df11 \
	        --dfuse-address 0x08000000:leave \
	        -D build/$(BASE_TARGET)Pro.bin
