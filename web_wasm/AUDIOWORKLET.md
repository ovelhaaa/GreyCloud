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

## Current Limitations & Next Steps
- **Shimmer / Pitch Shifter** functionality is disabled.
- **FDN 4x4** architecture is not implemented yet.
- **Preset Save/Load** functionality handles only default factory presets right now.
