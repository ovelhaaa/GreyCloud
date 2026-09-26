#include "PluginProcessor.h"
#include "TempoSyncUtils.h"
#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <iostream>
#include <utility>

namespace {
bool closeEnough(float a, float b, float e = .01f) { return std::abs(a - b) <= e; }
void set(CloudGreyVerbProcessor& p, const char* id, float v) {
    auto* parameter = p.getVTS().getParameter(id);
    parameter->setValueNotifyingHost(parameter->convertTo0to1(v));
}
class TestPlayHead final : public juce::AudioPlayHead {
public:
    double bpm = 120.0;
    juce::Optional<PositionInfo> getPosition() const override {
        PositionInfo p; p.setBpm(bpm); return p;
    }
};
struct Metrics {
    float peak = 0.0f;
    float inside = 0.0f;
    float cross[2] {};
    float last[2] {};
    bool hasPreviousBlock = false;
};
struct ClickLimits {
    float inside = 0.0f;
    float cross = 0.0f;
};
ClickLimits clickLimits(float baseline) {
    // The floor permits harmless floating-point/modulation noise. The multiplier
    // leaves substantial headroom over the transition-free measured baseline,
    // while a clearly discontinuous 0.5-scale jump still fails this test.
    return { std::max(.035f, baseline * 6.0f),
             std::max(.035f, baseline * 8.0f) };
}
bool bounded(const Metrics& metrics, const ClickLimits& limits) {
    // Independent guards: runaway signal, in-block click, boundary click.
    return metrics.peak < 16.0f
        && metrics.inside < limits.inside
        && metrics.cross[0] < limits.cross
        && metrics.cross[1] < limits.cross;
}
bool run(CloudGreyVerbProcessor& p, juce::MidiBuffer& midi, int n, int ordinal,
         Metrics& metrics) {
    juce::AudioBuffer<float> b(2, n);
    for (int i = 0; i < n; ++i) {
        const float x = .15f * std::sin(float((ordinal * n + i) * .019));
        b.setSample(0, i, x); b.setSample(1, i, x * .8f);
    }
    p.processBlock(b, midi);
    for (int ch = 0; ch < 2; ++ch) {
        const auto* d = b.getReadPointer(ch);
        for (int i = 0; i < n; ++i) {
            if (!std::isfinite(d[i])) return false;
            metrics.peak = std::max(metrics.peak, std::abs(d[i]));
            if (i) metrics.inside = std::max(metrics.inside, std::abs(d[i] - d[i - 1]));
        }
        if (metrics.hasPreviousBlock)
            metrics.cross[ch] = std::max(metrics.cross[ch], std::abs(d[0] - metrics.last[ch]));
        metrics.last[ch] = d[n - 1];
    }
    metrics.hasPreviousBlock = true;
    return true;
}
bool matrix(int n, double rate) {
    CloudGreyVerbProcessor p; p.prepareToPlay(rate, n);
    if (!p.areCoresReadyForTest()) return false;
    TestPlayHead host; p.setPlayHead(&host); juce::MidiBuffer midi;
    Metrics metrics; float baseline = 0;
    for (int block = 0; block < 112; ++block) {
        if (block == 8) set(p, "hqMode", 1); if (block == 20) set(p, "hqMode", 0);
        if (block == 28) p.setCurrentProgram(7); // Bright
        if (block == 40) p.setCurrentProgram(8); // Shimmer
        if (block == 52) p.setCurrentProgram(0); // SmallCloudRoom
        if (block == 60) set(p, "freeze", 1); if (block == 68) set(p, "freeze", 0);
        if (block == 72) set(p, "hardFreeze", 1); if (block == 80) set(p, "hardFreeze", 0);
        if (block == 84) { set(p, "hardFreeze", 1); p.setCurrentProgram(7); }
        if (block == 96) set(p, "hardFreeze", 0);
        if (!run(p, midi, n, block, metrics)) return false;
        if (block < 8) baseline = std::max(baseline, metrics.inside);
    }
    const auto limits = clickLimits(baseline);
    std::cout << "matrix " << n << " @ " << rate << " Hz: baseline=" << baseline
              << ", in-block=" << metrics.inside << '/' << limits.inside
              << ", cross L=" << metrics.cross[0] << '/' << limits.cross
              << ", R=" << metrics.cross[1] << '/' << limits.cross << '\n';
    return bounded(metrics, limits);
}
bool hostTempo() {
    CloudGreyVerbProcessor p; p.prepareToPlay(48000, 64);
    if (!p.areCoresReadyForTest()) return false;
    TestPlayHead host; p.setPlayHead(&host); juce::MidiBuffer midi;
    Metrics metrics;
    set(p, "preDelay", 1); // Manual maximum must remain distinguishable from sync.
    if (!run(p, midi, 64, 200, metrics) || p.getLastRuntimePreDelaySecondsForTest() >= 0) return false;
    set(p, "preDelaySync", 1); set(p, "syncDivision", 4); // 1/8
    for (double bpm : {120., 90., 180., 72.}) {
        host.bpm = bpm;
        if (!run(p, midi, 64, int(bpm), metrics)
            || !closeEnough(p.getLastRuntimePreDelaySecondsForTest(), .5f * 60.f / float(bpm))) return false;
    }
    set(p, "preDelaySync", 0);
    if (!run(p, midi, 64, 201, metrics) || p.getLastRuntimePreDelaySecondsForTest() >= 0) return false;
    set(p, "preDelaySync", 1); set(p, "syncDivision", 12); host.bpm = 72;
    return run(p, midi, 64, 202, metrics)
        && closeEnough(p.getLastRuntimePreDelaySecondsForTest(), 8.f * 60.f / 72.f);
}
bool snapshotParityBeforeFirstBlock() {
    CloudGreyVerbProcessor source; source.prepareToPlay(48000, 64);
    set(source, "mix", .71f); set(source, "texture", .37f); set(source, "freeze", 1);
    set(source, "feedback", .63f); set(source, "size", .42f); set(source, "sizeScale", 2.5f);
    set(source, "diffusion", .81f); set(source, "modDepth", .33f); set(source, "modRate", .27f);
    set(source, "damping", .44f); set(source, "lowDamping", .61f); set(source, "tone", .69f);
    set(source, "shimmer", .23f); set(source, "shimmerRatio", 3); set(source, "inputGain", .82f);
    set(source, "outputGain", .91f); set(source, "preDelay", .57f); set(source, "stereoWidth", 1.35f);
    set(source, "stereoCore", 0); set(source, "hardFreeze", 1); set(source, "reverseMix", .41f);
    set(source, "grainScan", .73f); set(source, "hqMode", 1); set(source, "preDelaySync", 0);
    set(source, "sizeSync", 0); set(source, "syncDivision", 7);
    juce::MemoryBlock state; source.getStateInformation(state);

    CloudGreyVerbProcessor restored; restored.prepareToPlay(48000, 64);
    restored.setStateInformation(state.getData(), int(state.getSize()));
    const auto p = restored.getCurrentDspParamsForTest();
    const bool paramsMatch = closeEnough(p.mix, .71f, .0001f) && closeEnough(p.texture, .37f, .0001f)
        && closeEnough(p.freeze, 1, .0001f) && closeEnough(p.feedback, .63f, .0001f)
        && closeEnough(p.size, .42f, .0001f) && closeEnough(p.sizeScale, 2.5f, .0001f)
        && closeEnough(p.diffusion, .81f, .0001f) && closeEnough(p.modDepth, .33f, .0001f)
        && closeEnough(p.modRate, .27f, .0001f) && closeEnough(p.damping, .44f, .0001f)
        && closeEnough(p.lowDamping, .61f, .0001f) && closeEnough(p.tone, .69f, .0001f)
        && closeEnough(p.shimmer, .23f, .0001f) && p.shimmerRatioIndex == 3
        && closeEnough(p.inputGain, .82f, .0001f) && closeEnough(p.outputGain, .91f, .0001f)
        && closeEnough(p.preDelay, .57f, .0001f) && p.preDelaySeconds < 0.0f
        && closeEnough(p.stereoWidth, 1.35f, .0001f) && !p.stereoCore && p.hardFreeze
        && closeEnough(p.reverseMix, .41f, .0001f) && closeEnough(p.grainScan, .73f, .0001f)
        && !p.clipOutput;
    return paramsMatch && restored.getCurrentDspHqModeForTest()
        && !restored.getCurrentPreDelaySyncForTest() && !restored.getCurrentSizeSyncForTest()
        && restored.getCurrentSyncDivisionForTest() == 7;
}
bool syncPreDelayParityBeforeFirstBlock() {
    CloudGreyVerbProcessor source; source.prepareToPlay(48000, 64);
    set(source, "preDelay", .5f); set(source, "preDelaySync", 1); set(source, "syncDivision", 7);
    juce::MemoryBlock state; source.getStateInformation(state);
    CloudGreyVerbProcessor restored; restored.prepareToPlay(48000, 64);
    TestPlayHead host; host.bpm = 120.0; restored.setPlayHead(&host);
    restored.setStateInformation(state.getData(), int(state.getSize()));
    const auto p = restored.getCurrentDspParamsForTest();
    return closeEnough(p.preDelay, .5f, .0001f) && closeEnough(p.preDelaySeconds, .5f, .0001f)
        && restored.getCurrentPreDelaySyncForTest() && restored.getCurrentSyncDivisionForTest() == 7;
}
bool stateRestore() {
    CloudGreyVerbProcessor source; source.prepareToPlay(48000, 64);
    set(source, "hqMode", 1); set(source, "preDelaySync", 1); set(source, "sizeSync", 1);
    set(source, "syncDivision", 12); set(source, "sizeScale", 2.5f);
    set(source, "mix", .71f); set(source, "feedback", .63f); set(source, "stereoWidth", 1.35f);
    juce::MemoryBlock state; source.getStateInformation(state);

    CloudGreyVerbProcessor restored; restored.prepareToPlay(48000, 64);
    TestPlayHead host; host.bpm = 90.0; restored.setPlayHead(&host);
    restored.setStateInformation(state.getData(), int(state.getSize()));
    if (!restored.areCoresReadyForTest()) return false;
    for (const auto& expected : std::initializer_list<std::pair<const char*, float>> {
             {"hqMode", 1}, {"preDelaySync", 1}, {"sizeSync", 1}, {"syncDivision", 12},
             {"sizeScale", 2.5f}, {"mix", .71f}, {"feedback", .63f}, {"stereoWidth", 1.35f} })
        if (!closeEnough(restored.getVTS().getRawParameterValue(expected.first)->load(), expected.second, .0001f)) return false;

    juce::MidiBuffer midi; Metrics transitionMetrics;
    for (int block = 0; block < 24; ++block)
        if (!run(restored, midi, 64, 300 + block, transitionMetrics)) return false;

    Metrics stableMetrics;
    for (int block = 24; block < 48; ++block)
        if (!run(restored, midi, 64, 300 + block, stableMetrics)) return false;

    const float baseline = stableMetrics.inside;
    const auto limits = clickLimits(baseline);
    std::cout << "restore: baseline=" << baseline
              << ", transition in-block=" << transitionMetrics.inside << '/' << limits.inside
              << ", cross L=" << transitionMetrics.cross[0] << '/' << limits.cross
              << ", R=" << transitionMetrics.cross[1] << '/' << limits.cross
              << ", stable in-block=" << stableMetrics.inside << '/' << limits.inside
              << ", cross L=" << stableMetrics.cross[0] << '/' << limits.cross
              << ", R=" << stableMetrics.cross[1] << '/' << limits.cross << '\n';
    return closeEnough(restored.getLastRuntimePreDelaySecondsForTest(), 8.f * 60.f / 90.f)
        && bounded(transitionMetrics, limits);
}
bool stateRestoreDuringPlayback() {
    CloudGreyVerbProcessor source; source.prepareToPlay(48000, 64);
    set(source, "hqMode", 1); set(source, "preDelaySync", 1); set(source, "sizeSync", 1);
    set(source, "syncDivision", 12); set(source, "sizeScale", 2.5f);
    set(source, "mix", .71f); set(source, "feedback", .63f); set(source, "stereoWidth", 1.35f);
    juce::MemoryBlock state; source.getStateInformation(state);

    CloudGreyVerbProcessor playback; playback.prepareToPlay(48000, 64);
    TestPlayHead host; host.bpm = 90.0; playback.setPlayHead(&host); juce::MidiBuffer midi;
    Metrics warmup;
    for (int block = 0; block < 12; ++block)
        if (!run(playback, midi, 64, 500 + block, warmup)) return false;
    playback.setStateInformation(state.getData(), int(state.getSize()));

    Metrics transitionMetrics;
    for (int block = 0; block < 24; ++block)
        if (!run(playback, midi, 64, 512 + block, transitionMetrics)) return false;
    Metrics stableMetrics;
    for (int block = 24; block < 48; ++block)
        if (!run(playback, midi, 64, 512 + block, stableMetrics)) return false;
    const auto limits = clickLimits(stableMetrics.inside);
    return playback.areCoresReadyForTest()
        && closeEnough(playback.getLastRuntimePreDelaySecondsForTest(), 8.f * 60.f / 90.f)
        && bounded(transitionMetrics, limits);
}
}
int main() {
    if (!closeEnough(TempoSyncUtils::getMsFromBpm(60, 12), 8000)) return 1;
    const auto required = CloudGreyVerb::requiredMemoryFloats(48000);
    std::vector<float> memory(required); CloudGreyVerb core;
    core.init(48000, memory.data(), memory.size());
    if (!core.isInitialized() || !required) return 2;
    for (int n : {16, 64, 256, 1024}) if (!matrix(n, 48000)) return 3;
    for (double r : {44100., 96000., 192000.}) if (!matrix(64, r)) return 4;
    if (!hostTempo()) return 5;
    if (!snapshotParityBeforeFirstBlock()) return 6;
    if (!syncPreDelayParityBeforeFirstBlock()) return 7;
    if (!stateRestore()) return 8;
    if (!stateRestoreDuringPlayback()) return 9;
    std::cout << "Realtime L/R cross-block, BPM mock, exact core init and state restore verified\n";
}
