# M3.1 — Preset Truth

`CloudGreyVerb::FactoryPreset` is the sole factory-program specification.
It contains the complete `CloudGreyVerb::Params` acoustic state plus the
plugin-only acoustic flags (`hqMode`, pre-delay/size sync and division).
The VST program list reads this catalogue directly, and the offline M3 bench
receives the same object rather than rebuilding a partial `Params` value.

## Pre-delay audit (48 kHz nominal)

| Preset | Pre-delay ms | Width | HQ | First wet/early arrival |
|---|---:|---:|---:|---:|
| SmallCloudRoom | 0 | 1.0 | no | 3.10 ms |
| BassAmbientWash | 20 | 1.5 | no | 23.27 ms |
| FrozenOrganPad | 0 | 1.2 | no | 3.44 ms |
| GreyholeDelayVerb | 40 | 1.0 | no | 43.48 ms |
| DarkLongCloud | 60 | 1.0 | no | 63.54 ms |
| GlitchSmear | 0 | 1.0 | no | 3.00 ms |
| AlwaysOnSubtle | 10 | 0.8 | no | 12.94 ms |
| BrightCloud | 20 | 1.2 | no | 23.31 ms |
| ShimmerCloud | 30 | 1.4 | yes | 33.33 ms |
| ReverseSmear | 0 | 1.2 | no | 3.25 ms |

`first wet/early arrival` is measured from a wet-only impulse render. It is
therefore pre-delay plus the first active early tap, not the cloud/FDN onset.
The first main FDN onset remains programme/material dependent and must not be
confused with this value.

## Reproducible render

Build `CloudGreyVerbM2Bench` (retained target name for compatibility) and run:

```text
CloudGreyVerbM2Bench build/m3
```

The layout is `build/m3/dry`, `build/m3/after/<preset>`, and
`build/m3/metrics`. Musical renders retain mix and gain; IR analysis is the
one explicit wet-only path. The deterministic snare is a decaying broadband
noise/body synthesis, replacing the prior Nyquist-heavy alternating samples.

## M3.1b measurement contract

Spectral measurements are offline Welch estimates over the explicitly named
region. They use a 4096-point FFT, a Hann window, 50% overlap (2048 samples),
and average stereo energetic power before deriving band RMS and the
power-weighted centroid. The five stable bands are 20–80, 80–200, 200–1000,
1000–4000, and 4000–16000 Hz. A final partial window is zero-padded; no FFT is
run in the audio callback.
`spectral_centroid_*` is specifically the power-weighted centroid over
20 Hz–16 kHz; DC and spectrum above 16 kHz do not enter that value.

Every musical row identifies four time boundaries. `full` is the entire
eight-second render. `active` spans the first through last input sample above
-60 dB relative to that source's peak. `tail` starts 75 ms after the last such
sample and ends with the render. Band columns are linear RMS; `_delta_db`
columns are 20 log10(output band RMS / dry band RMS). Centroids are in Hz.

`output_rms` is stereo energetic RMS,
`sqrt(mean((L^2 + R^2) / 2))`. The mono fold is `M=(L+R)/2`, and
`mono_delta_db=20 log10(rms(M)/output_rms)`. Mid and side use
`M=(L+R)/2` and `S=(L-R)/2`; `side_mid_ratio` is their RMS ratio.

The metrics directory now contains three durable tables:

* `ir_metrics.csv` contains wet-only impulse timing, energy, spatial, decay,
  peak and safety measurements. Its `energy_*` values are summed stereo energy.
* `musical_metrics.csv` contains only the ten named musical sources, their
  region boundaries, region spectra, dry deltas, level and mono definitions.
* `preset_summary.csv` aggregates exactly those ten musical rows per preset;
  impulse renders are never included.

### Host execution policy

`FactoryPreset::dsp` is portable acoustic state. `FactoryPreset::hqMode` is a
host execution policy. VST3 and Standalone honor it with JUCE two-times
`filterHalfBandFIREquiripple` oversampling and therefore run the core at
96 kHz for a 48 kHz host. WASM currently applies the same portable DSP state
at the native WebAudio rate and does **not** claim VST-HQ equivalence.

The dependency-light benchmark retains its historical linear midpoint/two-core-
steps approximation and labels every such row `render_mode=hq_approx`
(`normal` otherwise). In particular, ShimmerCloud is `hq_approx`; its timing
and 96 kHz core-path checks are useful, but its spectrum, aliasing, peak and RMS
are not JUCE/VST ground truth. Absolute ShimmerCloud HQ decisions must use a
VST/Standalone JUCE render. This explicit limitation avoids introducing a
second plugin engine into the measurement tool.

## Retuning

No DSP-parameter retune is included in this pass. The previous comparison was
not valid for the VST programs because several programs had a different
pre-delay and width. The canonical-state render is the baseline for a
subsequent A/B retune; it avoids presenting a change as musical evidence when
it was merely preset-state divergence.
