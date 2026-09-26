# M5 — Product UX, parameter semantics and presets

## Public parameter contract

APVTS IDs are persistence and host-automation contracts. M5 retains every
existing ID, including `lowDamping`, `preDelay`, `sizeScale`, `stereoCore`,
`hardFreeze`, `hqMode`, `preDelaySync`, `sizeSync`, `syncDivision`,
`reverseMix`, and `grainScan`. `FactoryPresetApvtsTest` freezes host names,
float ranges/defaults and both choice orders; it also verifies the supported
factory-to-APVTS mapping.

Host labels are musical without changing IDs: `lowDamping` is **Low Cut** and
`reverseMix` is **Reverse**. Mix-like controls show percent. Pre-Delay shows
milliseconds (`0.5 = 100 ms`, `1 = 200 ms`). Input/Output retain linear DSP
values 0–2 and display dB (`1 = 0.0 dB`, `0 = -∞ dB`).

## Tempo sync

Size and Pre-Delay intentionally share the single `syncDivision` parameter.
Their Sync buttons disable their manual controls when active; the division is
always visible. Runtime tempo conversion remains owned by `TempoSyncUtils`,
with its 120 BPM fallback and bounds. No host calls are made by paint code.

## Presets and Edited state

Factory programs remain the existing VST programs. The header displays the
factory name plus `• Edited` whenever the complete persisted parameter state
differs from that program; restoring the exact values clears the marker.
Previous/next buttons are shortcuts to the same program API and the combo
remains available. Double-clicking a continuous control restores its APVTS
default, never the currently loaded factory preset.

## JSON compatibility and transaction policy

Version 1 JSON accepts `app: GreyCloud` (legacy) and `app: Nimbus`; new files
brand themselves Nimbus while retaining version 1. Export writes actual APVTS
plain values and uses the current preset display name.

Import is parse → validate all known values → coherent APVTS commit → publish
one M4 transition target. Unknown IDs are ignored for forward compatibility.
Known values must be finite and inside their public range; otherwise nothing is
changed. This prevents the audio callback from observing a partial preset.

## Basic and advanced controls

Mix, Size, Feedback and Texture remain the macro row. Freeze/Hard Freeze are
visibly grouped. Tooltips explain the musical consequences of Texture,
Diffusion, Low Cut, Freeze, Hard Freeze, Grain Scan, Reverse and HQ (2×
processing with a CPU cost). Stereo Core, HQ and Grain Scan remain technical
controls without hiding musically central Freeze, Tone, Shimmer, Pre-Delay or
Width.
