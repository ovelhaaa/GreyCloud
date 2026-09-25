# M4 — Realtime Host Behavior & Transition Reliability

## Completion status

M4 is complete. The covered operations—preset changes, HQ changes, host tempo
changes, manual/sync changes, Freeze/Hard Freeze, and state restore—are finite,
bounded, cross-block safe per channel, dry-continuous, latency-stable, and
state-safe under the regression matrix. M5 is intentionally out of scope.

## Realtime ownership and transition policy

`AudioProcessorValueTreeState` is the sole persisted parameter state.
Factory-program selection is a host program index; a new processor applies
factory program 0 before audio runs, while restore replaces APVTS state and
never reapplies program 0. Factory selection completes its APVTS transaction
before publishing an atomic request. If that transaction overlaps audio, the
callback retains the preceding complete state. Individual parameter automation
remains direct.

The audio callback reads atomics and host tempo, selects pre-prepared Normal or
HQ cores, and processes samples without allocation, locks, filesystem access, or
host notifications. `prepareToPlay` owns memory, oversampling, and delay-line
preparation.

HQ and preset changes use one fixed policy:

`wet fade-out → reset/switch target wet engine → wet fade-in`

Only wet state is switched. No stale tail, freeze, grain, feedback, or
hard-freeze state transfers to an incompatible engine. The dry reference stays
continuous at fixed PDC latency. Normal's `latencyCompensation` may be reset to
discard obsolete wet history; `transitionDryDelay` is deliberately retained, so
the fixed-latency dry reference remains intact while the wet target refills.
Reported latency is always the oversampling latency; Normal uses internal
compensation so HQ OFF↔ON cannot alter host-reported latency or dry timing.

## Tempo-sync and capacity contract

`vst/TempoSyncUtils.h` is the source of truth for display order and conversion:
1/32 through 2/1, with 60–240 BPM accepted and invalid/missing tempo sanitized
to the documented range (120 BPM fallback for non-finite values). Manual
Pre-Delay stays persisted at 0–200 ms. A runtime-only `preDelaySeconds` carries
tempo-sync targets to DSP; it does not change the stored parameter range.

Large pre-delay target changes use a 20 ms stationary-tap crossfade; small
automation retains one-pole smoothing. Size sync maps the same tempo through
`secondsToSize` while preserving `sizeScale`; M3.2 voicing and factory presets
are not retuned.

| Profile | Pre-delay capacity | Approx. DSP memory @48 kHz | Sync limitation |
| --- | ---: | ---: | --- |
| H5 Low CPU | 0.25 s | 384,094 floats / ~1.47 MiB | requests above 0.25 s clamp |
| H5 Balanced | 1.0 s | 772,166 floats / ~2.95 MiB | requests above 1.0 s clamp |
| WASM | 1.0 s | 772,166 floats / ~2.95 MiB | requests above 1.0 s clamp |
| H7 | 2.0 s | 873,147 floats / ~3.33 MiB | requests above 2.0 s clamp |
| Desktop Studio | 8.0 s | 1,449,147 floats / ~5.53 MiB | full 2/1 at 60 BPM |

Desktop Studio therefore represents the full 60 BPM 2/1 request. Embedded
profiles and WASM safely clamp to their own history budget; this is a capacity
constraint, not different voicing. Every profile regression target initializes
from exactly `requiredMemoryFloats(sampleRate)`, with no desktop-sized magical
over-allocation.

## Regression evidence

`CloudGreyVerbRealtimeTransitionTest` covers exact core allocation,
manual↔sync pre-delay, BPM 120→90→180→72 with a real `AudioPlayHead`, 1/8→2/1,
large tap transitions, HQ, factory preset, combined preset+HQ, Freeze and Hard
Freeze, block sizes 16/64/256/1024, and 44.1/48/96/192 kHz.

It separately guards runaway peak (< 16.0), in-block deltas, and cross-block
N(last, channel C)→N+1(first, channel C) deltas for L and R independently. The
test prints the transition-free baseline during development. Its documented
limits are `max(0.035, baseline × 6)` for in-block deltas and
`max(0.035, baseline × 8)` for cross-block deltas: conservative headroom for
normal output from the ~0.15 input, while a clearly abnormal half-scale jump
fails.

The restore lifecycle is `prepareToPlay → setStateInformation → process` with a
valid playhead. It restores and verifies HQ ON, pre-delay and size sync ON, 2/1,
non-unity `sizeScale`, and custom Mix, Feedback, and Width; it then verifies
ready cores, finite bounded output, per-channel boundary safety, and retained
runtime sync timing.

Tail reporting remains conservatively finite at 30 seconds. Freeze is
conceptually indefinite but is not reported as infinity.
