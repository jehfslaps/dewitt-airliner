#!/usr/bin/make -f
# Airliner: the DJ Looper and the Vinyl Sim from Dewitt, side by side, as one effect.
# It shares Dewitt's engines and helpers (../plugin) and DPF (../dpf).
# Windows:  make WINDOWS=true CC=x86_64-w64-mingw32-gcc CXX=x86_64-w64-mingw32-g++ AR=x86_64-w64-mingw32-ar
# Three builds: KIND=both|looper|vinyl (default both)
FILES_DSP = AirlinerPlugin.cpp AirlinerPlatform.cpp
FILES_UI  = AirlinerUI.cpp

# KIND: both (default), looper or vinyl
ifeq ($(KIND),looper)
KIND_NUM = 1
KIND_NAME = Looper
else ifeq ($(KIND),vinyl)
KIND_NUM = 2
KIND_NAME = Vinyl
else
KIND_NUM = 0
KIND_NAME =
endif

NAME = Airliner$(KIND_NAME)
DPF_BUILD_DIR = build-$(KIND_NUM)

DPF_PATH = ../dpf
DPF_TARGET_DIR = bin

include $(DPF_PATH)/Makefile.plugins.mk

BUILD_CXX_FLAGS += -O2 -ffast-math -fno-finite-math-only -DAIRLINER=1 -DAIRLINER_KIND=$(KIND_NUM) -I../plugin

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
