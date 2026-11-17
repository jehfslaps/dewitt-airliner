#!/usr/bin/make -f
# Dewitt: build with DPF (https://github.com/DISTRHO/DPF) next to this folder as ../dpf
# Windows:  make WINDOWS=true CC=x86_64-w64-mingw32-gcc CXX=x86_64-w64-mingw32-g++ AR=x86_64-w64-mingw32-ar
# Mac: make   (VST3, CLAP and AU; see BUILDING.md)
FILES_DSP = YanceyPlugin.cpp YanceyPlatform.cpp YanceyDecode.cpp
FILES_UI  = YanceyUI.cpp
# Mac: drag-and-drop from Finder (Objective-C++)
ifneq (,$(findstring apple,$(shell $(or $(CC),cc) -dumpmachine 2>/dev/null)))
FILES_UI += YanceyDropMac.mm
endif

NAME = Dewitt
DPF_BUILD_DIR = build

DPF_PATH = ../dpf
DPF_TARGET_DIR = bin

include $(DPF_PATH)/Makefile.plugins.mk

BUILD_CXX_FLAGS += -O2 -ffast-math -fno-finite-math-only

# static FreeType (minimal module set, see ftmodule-min.h); point FT_DIR at your build
FT_DIR ?= $(abspath ../freetype)
export FT_DIR
ifeq ($(WINDOWS),true)
LINK_FLAGS += -static -static-libgcc -static-libstdc++
DGL_LIBS += $(FT_DIR)/libfreetype-win.a
else ifeq ($(MACOS),true)
DGL_LIBS += $(FT_DIR)/libfreetype-mac.a
else
DGL_LIBS += $(FT_DIR)/libfreetype-linux.a
endif

TARGETS += vst3
TARGETS += clap
ifeq ($(MACOS),true)
TARGETS += au
endif

all: $(TARGETS)
