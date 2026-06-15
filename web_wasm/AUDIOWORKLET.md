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
You must run a local HTTP server such as `python3 -m http.server 8080`, since AudioWorklets and modern JS modules cannot be loaded via `file://` protocol.

Navigate to: `http://localhost:8080/live.html`

## Testing with an external instrument (Mic Input)
1. Select **Mic / Instrument** in the audio source panel.
2. Click **⚡ START AUDIO**.
3. Accept the microphone browser prompt.
4. **WARNING**: Ensure you have headphones or are using an Audio Interface + Instrument (like a Guitar) to avoid acoustic feedback loop from the speakers reaching the microphone. Audio feedback loops can quickly turn into loud, unpleasant noises.

## Testing with an audio File
1. Make sure to **Start Audio**.
2. Select **File Player** in the audio source panel.
3. Choose an audio file like `.mp3` or `.wav`.
4. Use the custom player buttons (Play/Pause/Stop) to listen.
5. The audio node acts just like a real audio cable sending playback data through the WASM.

## Features specific to Live Mode
- **Real-Time DSP evaluation**: Sliders natively push automated `k-rate` data using AudioParams down to the audio processing thread block-by-block.
- **Visual Telemetry**: Peak, Freeze State, Loop Energy, and Safety Gain update natively every 20 internal buffer blocks avoiding console/UI clutter latency.
- **Freeze Modes**:
  - `Hold Freeze`: Transient momentary lock (sends freeze value `1.0` while holding it down, `0.0` when released, mimicking an actual momentary footswitch).
  - `Latch checkbox`: Modifies how the release is evaluated. 

## Technical Considerations
- `CloudGreyWorkletProcessor` handles block audio manipulation via the native ES6 Emscripten compiled `.js`. Memory mappings are reused to prevent real-time Allocation.
- Real-time processing chunk size is normally 128 frames (managed natively by the WebAudio API). The `cloud_grey_wasm.cpp` works efficiently block-by-block regardless of input sizing.
- To prevent heavy cross-thread lock overheads, messages are sent `k-rate` directly mapped via processor parameters array.

## Current Limitations & Next Steps
- **Shimmer / Pitch Shifter** functionality is disabled. Since Live mode allows you to actually hear how processing operates, evaluating Shimmer overhead here is a fantastic final milestone for a subsequent update.
- **Presets via Worklet message vs UI state**: We have synced the slider behaviors back up with JS mapping logic to keep visual sliders consistent.
- No preset load/save (serialization) is built yet, but defaults are easily available.
