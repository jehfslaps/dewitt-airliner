#!/bin/bash
# Gets DPF (the plug-in framework) at the commit Dewitt is tested with, as dpf/ next to plugin/,
# and applies Dewitt's three DPF patches (see BUILDING.md).
set -e
cd "$(dirname "$0")/.."
DPF_COMMIT=4238e1c7f0351bbe488d79f0899c540543ac7583
[ -d dpf/.git ] || git clone -q --recursive https://github.com/DISTRHO/DPF.git dpf
(cd dpf && git checkout -q "$DPF_COMMIT" && git submodule update -q --init --recursive)
cp plugin/dpf-patches/Makefile    dpf/dgl/Makefile
cp plugin/dpf-patches/nanovg.c    dpf/dgl/src/nanovg/nanovg.c
cp plugin/dpf-patches/fontstash.h dpf/dgl/src/nanovg/fontstash.h
echo "DPF ready (patched)"
