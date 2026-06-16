#!/bin/bash

# CloudGreyVerb - Emscripten Build Script
# Run this script if you have Emscripten installed: `./build.sh`
# It will generate cloud_grey.js and cloud_grey.wasm in the current directory.

# Make sure emcc is available in your PATH before running
if ! command -v emcc &> /dev/null
then
    echo "emcc command not found! Please install and activate Emscripten SDK."
    exit 1
fi

echo "Building CloudGreyVerb for WebAssembly..."

emcc cloud_grey_wasm.cpp ../src/dsp/cloud_grey_verb.cpp \
  -O3 \
  -std=c++17 \
  -fno-exceptions \
  -fno-rtti \
  -s WASM=1 \
  -s SINGLE_FILE=1 \
  -s MODULARIZE=1 \
  -s ALLOW_MEMORY_GROWTH=1 \
  -s ENVIRONMENT=web \
  -s EXPORT_NAME=CloudGreyModule \
  -s EXPORTED_FUNCTIONS='["_cgv_init","_cgv_reset","_cgv_set_param","_cgv_get_param","_cgv_set_preset","_cgv_process","_cgv_get_peak","_cgv_is_initialized","_cgv_shimmer_is_enabled","_cgv_get_freeze_state","_cgv_get_loop_energy","_cgv_get_safety_gain","_malloc","_free"]' \
  -s EXPORTED_RUNTIME_METHODS='["ccall","cwrap","HEAPF32"]' \
  -DCLOUD_GREY_PROFILE_H5_BALANCED=1 \
  -DCGV_ENABLE_SHIMMER=1 \
  -I../src/dsp \
  -o cloud_grey.js

if [ $? -eq 0 ]; then
    echo "Build Complete! ✅"
    echo "You can now run a local server (e.g., 'python3 -m http.server 8080') inside this folder."
else
    echo "Build failed. ❌"
    exit 1
fi
