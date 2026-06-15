#!/bin/bash

# CloudGreyVerb - Emscripten Build Script for AudioWorklet
# Run this script if you have Emscripten installed: `./build_live.sh`

if ! command -v emcc &> /dev/null
then
    echo "emcc command not found! Please install and activate Emscripten SDK."
    exit 1
fi

echo "Building CloudGreyVerb for WebAssembly Live Mode..."

emcc cloud_grey_wasm.cpp ../src/dsp/cloud_grey_verb.cpp \
  -O3 \
  -std=c++17 \
  -fno-exceptions \
  -fno-rtti \
  -s WASM=1 \
  -s MODULARIZE=1 \
  -s EXPORT_ES6=1 \
  -s EXPORT_NAME=CloudGreyModule \
  -s ALLOW_MEMORY_GROWTH=1 \
  -s EXPORTED_FUNCTIONS='["_cgv_init","_cgv_reset","_cgv_set_param","_cgv_get_param","_cgv_set_preset","_cgv_process","_cgv_get_peak","_cgv_get_freeze_state","_cgv_get_loop_energy","_cgv_get_safety_gain","_cgv_is_initialized","_malloc","_free"]' \
  -s EXPORTED_RUNTIME_METHODS='["HEAPF32"]' \
  -DCLOUD_GREY_PROFILE_H5_BALANCED=1 \
  -I../src/dsp \
  -o cloud_grey_live.js

if [ $? -eq 0 ]; then
    echo "Live Build Complete! ✅"
    # Create the live.html locally served
else
    echo "Build failed. ❌"
    exit 1
fi
