# M3.2 — Musical Retuning

## Evidence and scope

The unmodified head was rendered before tuning to `build/m32/before`; the
retuned state is in `build/m32/after`. Each contains `ir_metrics.csv`,
`musical_metrics.csv`, `preset_summary.csv`, dry sources and the complete
10-preset × 10-source A/B WAV render set. These build artifacts are ignored by
Git intentionally.

The topology, early-tap timing, FDN order/matrix, granular/reverse/shimmer
algorithms, freeze path, Safety Guard, mix law and metric definitions were not
changed. `fdn_ir_analyzer` now includes `ReverseSmear`, so the complete
canonical catalogue is represented in the IR CSV.

## Result

| Preset | Intent | Changed? | Main change | IR RT60 before → after (s) | Gain median before → after (dB) | Mono median before → after (dB) | Verdict |
| --- | --- | --- | --- | ---: | ---: | ---: | --- |
| AlwaysOnSubtle | invisible depth | No | — | 0.55 → 0.55 | -1.14 → -1.14 | -0.01 → -0.01 | Keep: attached and effectively mono-safe. |
| SmallCloudRoom | intimate cloud room | No | — | 0.73 → 0.73 | -2.60 → -2.60 | -0.09 → -0.09 | Keep: distinct from AlwaysOn without chorus. |
| BassAmbientWash | warm wide wash | Yes | Low Cut .68 → .63; output .94 → .92 | 1.90 → 1.91 | -2.85 → -3.03 | -0.11 → -0.11 | More useful low-mid tail, no Side-dependent bass. |
| BrightCloud | airy luminous cloud | Yes | feedback .72 → .70; damping .66 → .64; tone .72 → .68 | 3.41 → 3.16 | -2.90 → -2.82 | -0.16 → -0.18 | Clearer/shorter cloud, not a shimmer proxy. |
| GreyholeDelayVerb | enormous moving space | Yes | pre-delay 40 → 36 ms | 13.01 → 13.01 | -3.96 → -3.90 | -0.28 → -0.29 | Reduced gap while retaining depth and motion. |
| DarkLongCloud | deep cinematic darkness | Yes | damping .32 → .35; tone .32 → .35 | 15.89 → 15.92 | -7.47 → -7.47 | -0.15 → -0.15 | Dark remains dark; tail is less merely muffled. |
| ShimmerCloud | harmonics from tail | No | — | 2.47 → 2.47 | -5.73 → -5.73 | -0.28 → -0.28 | Preserve pending VST HQ listening. |
| FrozenOrganPad | reverb instrument | No | — | 3.64 → 3.64 | -4.39 → -4.39 | -0.52 → -0.52 | Preserve deliberate wet/instrument behavior. |
| GlitchSmear | intentional unstable smear | No | — | 0.72 → 0.72 | -2.97 → -2.97 | -0.04 → -0.04 | Keep extreme by design; Safety Guard stays inactive. |
| ReverseSmear | backwards bloom into space | No | — | 2.41 → 2.41 | -3.76 → -3.76 | -0.28 → -0.28 | Keep: reverse scan and cloud body remain explicit. |

IR spectral-centroid values are deliberately omitted from the table for
pre-delayed programmes: the legacy short-window spectral measurement occurs
before their wet arrival and is therefore not a valid tail-tonality measure.
The retained band and RT60 values are in the CSV artifacts.

## Parameter register

- `BassAmbientWash`: `lowDamping` (Low Cut) `.68 → .63`; `outputGain` `.94 → .92`.
- `BrightCloud`: `feedback` `.72 → .70`; `damping` `.66 → .64`; `tone` `.72 → .68`.
- `GreyholeDelayVerb`: `preDelay` `.20 → .18` (40 → 36 ms).
- `DarkLongCloud`: `damping` `.32 → .35`; `tone` `.32 → .35`.

All other factory parameters are unchanged. The resulting motion labels are:
AlwaysOn/SmallRoom `Static / Natural`; Bass `Subtle Movement`; Bright
`Cloud Movement`; Greyhole `Cloud / Strong`; DarkLong `Slow Cloud`; Shimmer
`Subtle / Cloud`; Frozen `Strong / Instrument`; Glitch `Extreme`; Reverse
`Strong / Creative`.

## Low end, levels and safety

The BassAmbient bass-source fold-down remains -0.01 dB and its Side/Mid ratio
remains 0.053, so the lower Low Cut did not make the fundamental rely on Side.
Its IR low/mid RT60 is 1.92/2.08 s before and 1.95/2.10 s after: a small body
increase, not a sub runaway. Across the full musical suite, every preset's
minimum Safety Guard gain is 1.0; no preset uses clipping for normalization.

DarkLong's IR low/mid/high RT60 changes 15.66/15.91/15.23 s to
15.74/15.97/15.27 s. BrightCloud's corresponding change is
3.68/3.68/3.11 s to 3.39/3.41/2.89 s, consistent with a cleaner, less
fatiguing late balance rather than a gain increase.

## Listening and HQ limitation

This pass used deterministic renders and measurement only in this checkout;
no physical VST/standalone HQ audition device was available. Therefore these
are measured retuning decisions, not a claim of completed subjective approval.
`ShimmerCloud` was intentionally not retuned: its offline render has
`hq_approx` status, so its high-frequency spectrum, aliasing and peak behavior
must be accepted or altered only after VST/standalone HQ listening with JUCE
2× oversampling. Required manual checks are integrated shimmer onset, no
piercing octave/HF runaway, and freeze transitions (on/hold/new input/off).

## Regression and CI

Run the configured Release CTest suite from `vst/build` before merge, then
confirm the pull-request workflow reports VST build, standalone build, CTest
and WASM smoke green. CI has not been submitted or observed by this local
retuning pass.
