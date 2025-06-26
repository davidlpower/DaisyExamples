# Project Names
BASE_TARGET = MidiLooper
TARGET := $(BASE_TARGET)
CPP_SOURCES := $(BASE_TARGET).cpp

ifeq ($(BETA),1)
	TARGET := $(BASE_TARGET)ProBeta
	CPP_SOURCES := $(BASE_TARGET)ProBeta.cpp
	EXTRA_FLAGS += -DBETA
endif

USE_DAISYSP_LGPL = 1

# Library Locations
LIBDAISY_DIR = ../../libDaisy/
DAISYSP_DIR = ../../DaisySP/

# Core location, and generic Makefile.
SYSTEM_FILES_DIR = $(LIBDAISY_DIR)/core
include $(SYSTEM_FILES_DIR)/Makefile

# Pass preprocessor flags
CFLAGS += $(EXTRA_FLAGS)
CPPFLAGS += $(EXTRA_FLAGS)