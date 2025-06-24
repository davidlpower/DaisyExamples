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
