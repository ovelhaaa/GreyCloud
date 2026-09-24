# Milestone 3 — musical voicing

## Intent

| Preset | Intent |
|---|---|
| AlwaysOnSubtle | invisible glue |
| SmallCloudRoom | intimate, dense cloud room |
| BassAmbientWash | warm wide wash without swallowing the fundamental |
| BrightCloud | open, clear, airy cloud |
| GreyholeDelayVerb | huge moving reverb-delay |
| DarkLongCloud | deep, dark cinematic tail |
| ShimmerCloud | shimmer integrated into the tail |
| FrozenOrganPad | freezable space that behaves like an instrument |
| GlitchSmear | extreme creative granular smear |

## Changes

The early/cloud/late architecture was retained. Early level now maps from 0.56
to 0.30 over Size; cloud glue maps from 0.10 to 0.16 over Diffusion. This keeps
small spaces attached while keeping large spaces late-tail led.

Dynamic damping was retuned from `1 / (1 + 8 * envelope)` to a bounded bloom
curve, `0.58 + 0.42 / (1 + 3 * envelope)`. It preserves attack clarity but
prevents the tail from becoming abruptly dark during normal notes.

Room and subtle presets use near-static modulation (0.05 and 0.02). Bright,
Greyhole and DarkLong retain progressively more movement. Low Cut was assigned
per preset; BassAmbientWash uses 0.68 to protect 40–200 Hz buildup.

## Deterministic reference renders

`CloudGreyVerbM2Bench` is now the reproducible M3 render bench despite its
historical target name. It produces ten synthetic musical sources in `dry/`
and all 90 preset/source renders underneath their preset names:

```powershell
cmake --build build/m3 --target CloudGreyVerbM2Bench
.\build\m3\CloudGreyVerbM2Bench.exe build\m3\after
.\build\m3\CloudGreyVerbM2Bench.exe build\m3\metrics --metrics
```

The metric-only invocation prints first arrival, six energy windows,
early/late ratio, centroid, RT60 estimate, early/late correlation, M/S ratios,
peak, RMS and minimum Safety Gain. Current post-tuning IR RT60 estimates span
0.72 s (SmallCloudRoom), 0.73 s (AlwaysOnSubtle), 1.85 s (BassAmbientWash),
3.26 s (BrightCloud), 13.30 s (GreyholeDelayVerb), and 18.03 s
(DarkLongCloud). Safety Gain remained 1.0 for every factory IR.

For a historical A/B, run the same command from the parent revision into
`build/m3/before`, then run it from this revision into `build/m3/after`.

## Regression result

The desktop milestone test and all four M2 profile tests pass using the DSP
CMake build (`ctest --test-dir build/m3 -C Release --output-on-failure`).
