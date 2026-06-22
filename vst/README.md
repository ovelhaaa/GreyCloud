# Cloud Grey Verb - VST/AU Plugin

This directory contains the scaffolding to build Cloud Grey Verb as a native VST3 / AudioUnit plugin using the JUCE framework. 

It shares the exact same core DSP code (`src/dsp`) as the WebAssembly, React, and STM32 versions.

## Prerequisites
- CMake (3.20 or newer)
- A C++ Compiler (GCC, Clang, or MSVC)
- _JUCE will be automatically fetched and configured by CMake._

## How to Build

1. Open a terminal in the root directory of this workspace and navigate to the `vst` folder.
2. Generate the build system using CMake:

   ```bash
   cmake -B build
   ```

3. Build the plugin:

   ```bash
   cmake --build build --config Release
   ```

4. The built VST3 file will be located inside `build/CloudGreyVerbPlugin_artefacts/Release/VST3/CloudGreyVerb.vst3`.

## Architecture Details
- `PluginProcessor.cpp`: Wraps the `CloudGreyVerb` core inside JUCE's `AudioProcessor`, maintaining the state (`AudioProcessorValueTreeState`) and audio life-cycle block callbacks.
- `PluginEditor.cpp`: Renders a minimal rotary knob interface for the DSP parameters.
