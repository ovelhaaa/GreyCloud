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

## Retuning

No DSP-parameter retune is included in this pass. The previous comparison was
not valid for the VST programs because several programs had a different
pre-delay and width. The canonical-state render is the baseline for a
subsequent A/B retune; it avoids presenting a change as musical evidence when
it was merely preset-state divergence.
