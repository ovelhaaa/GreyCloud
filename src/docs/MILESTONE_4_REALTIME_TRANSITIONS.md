# M4 — Realtime Host Behavior & Transition Reliability

## Realtime ownership audit

`AudioProcessorValueTreeState` owns persisted parameters and is the only state
serialized by `getStateInformation`. Factory-program selection is a host program
index, not proof that later manual edits still match that program. A new instance
applies factory program 0 before audio can run; state restore replaces that APVTS
state and never reapplies program 0.

`processBlock` runs on the audio thread. It reads APVTS atomics, obtains host
tempo when available, selects the prepared normal or HQ core, and performs the
sample loop. No allocation, lock, filesystem operation, or host notification is
performed there. `prepareToPlay` owns core memory, oversampling, and latency-line
preparation. Program/state changes occur on the host/message side and publish an
atomic transition request only after their APVTS transaction completes.

The normal and HQ cores have independent memory. Both are prepared ahead of
time. Reported latency is the oversampling latency at all times; normal mode uses
an internal compensation delay, so toggling HQ does not change the host-reported
latency or dry timing. To avoid stale tails, an HQ change resets the incoming
core (and its matching oversampling/latency state); it intentionally does not
attempt to transfer a mathematically identical tail.

Preset changes retain the existing short reset transition policy: the previous
engine is held while the transition begins, then both engine histories are reset
before the new acoustic state fades in. Freeze, grain, feedback, and hard-freeze
state therefore cannot leak into an incompatible preset. `freeze`, `hardFreeze`,
HQ, sync enable, division, and stereo-core are sampled as discrete values, not
interpolated DSP controls.

## Tempo-sync contract

`vst/TempoSyncUtils.h` is the source of truth for display order, conversion and
tests: 1/32 through 2/1. Supported host tempo is 60–240 BPM. Missing, zero,
negative, non-finite, or out-of-range tempo is sanitized to the nearest supported
value (with 120 BPM as the non-finite fallback).

The visible manual Pre-Delay remains 0–200 ms. A runtime-only
`Params::preDelaySeconds` override carries sync time to the DSP without changing
the persisted parameter ID/range. Physical history is 8 seconds plus interpolation
guard samples: at 60 BPM a quarter note is one second and 2/1 spans eight quarter
notes. Large target changes use a 20 ms
crossfade between stationary delay taps; small automation changes retain the
existing one-pole smoothing. This prevents the former silent 200 ms clamp and
avoids a long moving-read-head Doppler sweep.

Size sync continues to map the same tempo value through `secondsToSize` while
retaining `sizeScale`; its acoustic range remains independently bounded by the
frozen M3.2 size policy.

## Host policy

Tail reporting stays deliberately finite at 30 seconds. It is conservative for
normal long programs while allowing hosts to schedule correctly; Freeze is
conceptually indefinite but is not reported as infinity.

`lowDamping` remains the persisted ID but is exposed as **Low Cut**. Historical
"ducking" comments describe the dynamic damping/Bloom envelope, not a new
algorithm or parameter identity.

## Regression coverage

`CloudGreyVerbRealtimeTransitionTest` verifies the division math at 120 BPM,
valid tempo range/fallback behavior, 8-second capacity, custom-state round trip,
and finite/bounded output during sync, HQ, freeze, preset, and manual/sync
transitions across block sizes 16–1024 and 44.1/48/96/192 kHz. Its gross-click
guard flags a sample-to-sample delta above 8 for a 0.15 input; it is a regression
tripwire, not a psychoacoustic loudness metric.
