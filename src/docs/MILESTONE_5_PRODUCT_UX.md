# M5 — Product UX, parameter semantics and presets

## Public parameter contract

APVTS IDs are persistence and host-automation contracts. M5 retains every
existing ID, including `lowDamping`, `preDelay`, `sizeScale`, `stereoCore`,
`hardFreeze`, `hqMode`, `preDelaySync`, `sizeSync`, `syncDivision`,
`reverseMix`, and `grainScan`. `FactoryPresetApvtsTest` freezes host names,
float ranges/defaults and both choice orders; it also verifies the supported
factory-to-APVTS mapping.

Host labels are musical without changing IDs: `lowDamping` is **Low Cut** and
`reverseMix` is **Reverse Mix**. The Nimbus UI intentionally shortens the
`reverseMix` label to **Reverse**; the ID, host name and UI label are separate
contracts. Mix-like controls show percent. Pre-Delay shows
milliseconds (`0.5 = 100 ms`, `1 = 200 ms`). Input/Output retain linear DSP
values 0–2 and display dB (`1 = 0.0 dB`, `0 = -∞ dB`).

## Tempo sync

Size and Pre-Delay intentionally share the single `syncDivision` parameter.
Their Sync buttons disable their manual controls when active; the division is
always visible alongside read-only feedback such as `120 BPM • 1/4 = 500 ms`,
which applies to both **Pre-Delay Sync** and **Size Sync**. Runtime tempo
conversion remains owned by `TempoSyncUtils`, with its 120 BPM fallback and
bounds. No host calls are made by paint code; the UI reads the processor's
atomic display BPM and APVTS state only. Size's approximate display always
uses the current persisted `sizeScale`; while Size Sync is enabled, the
disabled manual knob is not presented as the effective time.

## Presets and Edited state

Factory programs remain the existing VST programs. The header displays the
factory name plus `• Edited` whenever the complete persisted parameter state
differs from that program; restoring the exact values clears the marker. The
selected factory-program index is stored as non-automatable state metadata, so
an edited BrightCloud session restores as `BrightCloud • Edited`. Old states
without metadata and invalid indices safely fall back to program zero.
Previous/next buttons are shortcuts to the same program API and the combo
remains available. Double-clicking a continuous control restores its APVTS
default in **plain** units (`convertFrom0to1(getDefaultValue())`), never the
currently loaded factory preset. For example, Input/Output Gain reset to `1.0`
instead of normalized `0.5`.

## JSON compatibility and transaction policy

Version 1 JSON accepts `app: GreyCloud` (legacy) and `app: Nimbus`; new files
brand themselves Nimbus while retaining version 1. Export writes actual APVTS
plain values and uses the current preset display name.

Import is parse → validate all known values → coherent APVTS commit → publish
one M4 transition target. Unknown IDs are ignored for forward compatibility.
Known values must be actual JSON numbers (or native JSON booleans for boolean
parameters), finite and inside their public range; strings such as
`"banana"` are rejected rather than coerced. Choices (`shimmerRatio` and
`syncDivision`) must be valid integral indices, and numeric bool values must
be exactly 0 or 1. Otherwise nothing is changed or published. This prevents
the audio callback from observing a partial preset. The schema parser rejects
unknown apps, unsupported versions and malformed/non-object parameter maps;
unknown parameter IDs remain safely ignored for forward compatibility.

## Basic and advanced controls

Mix, Size, Feedback and Texture remain the macro row. Freeze/Hard Freeze are
visibly grouped. Tooltips explain the musical consequences of Texture,
Diffusion, Low Cut, Freeze, Hard Freeze, Grain Scan, Reverse and HQ (2×
processing with a CPU cost). Stereo Core, HQ and Grain Scan remain technical
controls without hiding musically central Freeze, Tone, Shimmer, Pre-Delay or
Width.
