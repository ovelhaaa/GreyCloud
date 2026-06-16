# GreyCloud - Live AudioWorklet Mode

## Overview
This mode uses the standard `AudioWorklet` API in the browser to run the internal C++ `CloudGreyVerb` engine logic in real-time, receiving input directly from a local audio file or microphone/instrument interface.

## Build the live WASM module
To generate the ES6 compatible `.js` and `.wasm` files needed for the AudioWorklet:

```bash
cd web_wasm
chmod +x build_live.sh
./build_live.sh
```

*(Note that the regular `build.sh` still exists and is used by `index.html` for the offline rendering module, as it uses the regular non-ES6 global export).*

## How to run
You must run a local HTTP server such as:

```bash
cd web_wasm
python3 -m http.server 8080
```

Navigate to: `http://localhost:8080/live.html`

## Recommended Testing Flow
1. Open Live Mode (`live.html`).
2. Click **⚡ START AUDIO**.
3. Select **File Player** (selected by default to avoid acoustic feedback).
4. Load an audio file (loop).
5. Test preset `AlwaysOnSubtle`.
6. Test preset `BassAmbientWash`.
7. Test **Hold Freeze** button.
8. Test **Latch** toggle.
9. Test **Reset Tail** button.
10. Only after verifying stability, test **Mic / Instrument** with headphones.

## Warnings and Cautions
- **Headphones Required**: If you use the Mic Input with speakers, the acoustic feedback loop can quickly turn into loud, unpleasant noises and trigger the Safety Guard.
- **Safety Gain Indicator**: If the Safety Gain meter turns red, the preset is being aggressively limited by the DSP Safety Guard to protect the audio graph from runaway loops.

## Features specific to Live Mode
- **Real-Time DSP evaluation**: Sliders natively push automated `k-rate` data using AudioParams down to the audio processing thread block-by-block.
- **Visual Telemetry**: Peak, Freeze State, Loop Energy, and Safety Gain update natively every 20 internal buffer blocks avoiding console/UI clutter latency.
- **Freeze Modes UX**:
  - `Hold Freeze`: Transient momentary lock with Pointer capture support for robust touch/mouse handling.
  - `Latch checkbox`: Modifies how the release is evaluated. 

## Technical Considerations
- `CloudGreyWorkletProcessor` handles block audio manipulation via the native ES6 Emscripten compiled `.js`. Memory mappings are reused optimally without allocation in the hot path.
- Processor uses parameter caching to avoid redundant `wasm._cgv_set_param()` cycles.
- Before `CloudGreyVerb` is properly initialized from WASM, the AudioWorklet handles silence.

## User Presets
Custom presets can be saved, updated, deleted, exported, and imported using LocalStorage.
- **Save Current**: Saves the current slider state as a new preset.
- **Update Selected**: Updates an existing custom preset (Factory presets cannot be overwritten).
- **Export/Import JSON**: Allows backing up presets to a file for portability or sharing.
- Note: LocalStorage is local to the device/browser. Use JSON export for true backup.

## Smooth Preset Switching
Parameter changes (such as slider drags or entire preset swaps) are smoothed natively using the WebAudio `setTargetAtTime` API to avoid audio clicks and artifacts during harsh transitions. Note that radical changes to `size` or `feedback` may still cause temporary audible artifacts, as a characteristic of delay-line interpolation.

## MIDI Control
Web MIDI is now fully supported in Live Mode to allow hardware control over the DSP parameters.

- **Enable MIDI**: Click the "Enable MIDI" button under the MIDI Control panel to request browser access.
- **Select Input**: Once authorized, select your hardware controller from the dropdown menu.
- **MIDI Learn**: 
  - Select a target parameter (e.g., Mix, Size, Freeze) in the dropdown next to the MIDI Learn button.
  - Click "MIDI Learn". The button will turn red.
  - Move a CC knob or press a Note on your controller.
  - The map is saved automatically to LocalStorage across sessions.
- **Default CC Mappings**:
  - `CC 1`: Mix
  - `CC 2`: Texture
  - `CC 3`: Feedback
  - `CC 4`: Size
  - `CC 5`: Tone
  - `CC 64`: Freeze (values >= 64 enable hold freeze, values < 64 release it unless Latch is active).
- **Program Change**: Sending a MIDI Program Change will cycle through the Factory Presets automatically modulo length.
- **Freeze Mapping via Notes**: You can map a MIDI note directly to the Freeze parameter using MIDI Learn. Pressing the note engages the freeze, and releasing the note releases it.
- **Note**: The Web MIDI API requires a secure context (localhost or HTTPS), and in some browsers, explicit user permission must be granted.

## WASM loading model

The GitHub Pages build uses Emscripten `SINGLE_FILE=1`.

This embeds the `.wasm` payload inside:

- `cloud_grey_live.js` for AudioWorklet Live Mode
- `cloud_grey.js` for Offline Render Mode

This avoids AudioWorklet failures caused by Emscripten glue trying to resolve `cloud_grey_live.wasm` using APIs unavailable inside `AudioWorkletGlobalScope`, such as `URL`.

If Live Mode fails with:

`Worklet Error: URL is not defined`

make sure `build_live.sh` uses `-s SINGLE_FILE=1` and that `cloud_grey_worklet_processor.js` calls `CloudGreyModule()` without `wasmBinary`.

## GitHub Pages

The live app is available at:

https://ovelhaaa.github.io/GreyCloud/web_wasm/live.html

The GitHub Pages workflow builds:

- cloud_grey_live.js
- cloud_grey.js


before deployment.

Do not open live.html directly from the filesystem. AudioWorklet and WASM require a secure context such as localhost or HTTPS.

## Troubleshooting: Unable to load a worklet's module

If Live Mode fails with:

`Failed to init audio: Unable to load a worklet's module`

Check in DevTools > Network if any of these files returned 404:

- cloud_grey_worklet_processor.js
- cloud_grey_live.js

If `cloud_grey_live.js` is missing, the WASM build did not run before deployment.

When running manually, ensure you run:

```bash
cd web_wasm
chmod +x build_live.sh
./build_live.sh
python3 -m http.server 8080
```

Then open:

`http://localhost:8080/live.html`

Do not open the HTML directly from the filesystem.

## Current Limitations & Next Steps
- **FDN 4x4** architecture is not implemented yet.

## Shimmer (Experimental)
An experimental Shimmer effect (1 octave up pitch-shifting) has been added to the internal feedback loop. 
- **Dynamic LP**: A quadratic lowpass filter (varying from ~5500Hz down to 3800Hz) is applied to the shimmer tail, allowing celestial brightness at low levels while preventing harsh metallic resonance at higher amounts.
- **Ducking**: A fast-attack envelope tracker listens to transients and applies soft ducking to the shimmer feedback injection. This keeps the initial transient punchy and untainted, allowing the octave bloom to push forward during the decay.
- **Stereo Widening**: Shimmer utilizes a 7ms delay offset on the right channel within its pitch-shift windows, adding wide stereo bloom while remaining comb-filter safe upon mono downmix.
- **Parameter Index**: It uses index `12` in parameter mapping arrays. High compatibility with older presets (defaults to `0.0`).
- **Performance**: While highly optimized compared to FFT alternatives, it consumes about 2x the CPU of the standard cloud mode. To omit it completely for extreme budget scenarios, set `-DCGV_ENABLE_SHIMMER=0`.
