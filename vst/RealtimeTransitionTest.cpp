#include "PluginProcessor.h"
#include "TempoSyncUtils.h"
#include <algorithm>
#include <cmath>
#include <iostream>

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
bool run(CloudGreyVerbProcessor& p, juce::MidiBuffer& midi, int n, int ordinal,
         float& peak, float& inside, float& cross, float& last) {
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
            peak = std::max(peak, std::abs(d[i]));
            if (i) inside = std::max(inside, std::abs(d[i] - d[i - 1]));
        }
        if (ordinal) cross = std::max(cross, std::abs(d[0] - last));
        if (!ch) last = d[n - 1];
    }
    return true;
}
bool matrix(int n, double rate) {
    CloudGreyVerbProcessor p; p.prepareToPlay(rate, n);
    if (!p.areCoresReadyForTest()) return false;
    TestPlayHead host; p.setPlayHead(&host); juce::MidiBuffer midi;
    float peak = 0, inside = 0, cross = 0, last = 0, baseline = 0;
    for (int block = 0; block < 112; ++block) {
        if (block == 8) set(p, "hqMode", 1); if (block == 20) set(p, "hqMode", 0);
        if (block == 28) p.setCurrentProgram(7); // Bright
        if (block == 40) p.setCurrentProgram(8); // Shimmer
        if (block == 52) p.setCurrentProgram(0); // SmallCloudRoom
        if (block == 60) set(p, "freeze", 1); if (block == 68) set(p, "freeze", 0);
        if (block == 72) set(p, "hardFreeze", 1); if (block == 80) set(p, "hardFreeze", 0);
        if (block == 84) { set(p, "hardFreeze", 1); p.setCurrentProgram(7); }
        if (block == 96) set(p, "hardFreeze", 0);
        if (!run(p, midi, n, block, peak, inside, cross, last)) return false;
        if (block < 8) baseline = std::max(baseline, inside);
    }
    // Cross-block N(last)->N+1(first) guard, separate from runaway guard.
    return peak < 16.f && inside < 8.f && cross < std::max(.50f, baseline * 12.f);
}
bool hostTempo() {
    CloudGreyVerbProcessor p; p.prepareToPlay(48000, 64);
    if (!p.areCoresReadyForTest()) return false;
    TestPlayHead host; p.setPlayHead(&host); juce::MidiBuffer midi;
    float peak = 0, inside = 0, cross = 0, last = 0;
    set(p, "preDelay", 1); // Manual maximum must remain distinguishable from sync.
    if (!run(p, midi, 64, 200, peak, inside, cross, last) || p.getLastRuntimePreDelaySecondsForTest() >= 0) return false;
    set(p, "preDelaySync", 1); set(p, "syncDivision", 4); // 1/8
    for (double bpm : {120., 90., 180., 72.}) {
        host.bpm = bpm;
        if (!run(p, midi, 64, int(bpm), peak, inside, cross, last)
            || !closeEnough(p.getLastRuntimePreDelaySecondsForTest(), .5f * 60.f / float(bpm))) return false;
    }
    set(p, "preDelaySync", 0);
    if (!run(p, midi, 64, 201, peak, inside, cross, last) || p.getLastRuntimePreDelaySecondsForTest() >= 0) return false;
    set(p, "preDelaySync", 1); set(p, "syncDivision", 12); host.bpm = 72;
    return run(p, midi, 64, 202, peak, inside, cross, last)
        && closeEnough(p.getLastRuntimePreDelaySecondsForTest(), 8.f * 60.f / 72.f);
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
    CloudGreyVerbProcessor a; a.prepareToPlay(48000, 64); set(a, "mix", .71f);
    juce::MemoryBlock state; a.getStateInformation(state);
    CloudGreyVerbProcessor b; b.prepareToPlay(48000, 64); b.setStateInformation(state.getData(), int(state.getSize()));
    if (!b.areCoresReadyForTest() || !closeEnough(a.getVTS().getRawParameterValue("mix")->load(), b.getVTS().getRawParameterValue("mix")->load())) return 6;
    std::cout << "Realtime cross-block, BPM mock, core init and restore verified\n";
}
