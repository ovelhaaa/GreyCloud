#include "PluginProcessor.h"
#include "TempoSyncUtils.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>

namespace {
bool closeEnough(float actual, float expected, float epsilon = 0.01f) {
    return std::abs(actual - expected) <= epsilon;
}

void set(CloudGreyVerbProcessor& processor, const char* id, float value) {
    auto* parameter = processor.getVTS().getParameter(id);
    parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
}

bool finiteAndSane(const juce::AudioBuffer<float>& buffer, float& peak, float& maxDelta) {
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel) {
        const float* data = buffer.getReadPointer(channel);
        for (int i = 0; i < buffer.getNumSamples(); ++i) {
            if (!std::isfinite(data[i])) return false;
            peak = std::max(peak, std::abs(data[i]));
            if (i > 0) maxDelta = std::max(maxDelta, std::abs(data[i] - data[i - 1]));
        }
    }
    return true;
}

bool processTransitionMatrix(int blockSize, double sampleRate) {
    CloudGreyVerbProcessor processor;
    processor.prepareToPlay(sampleRate, blockSize);
    juce::MidiBuffer midi;
    float peak = 0.0f, maxDelta = 0.0f;
    for (int block = 0; block < 80; ++block) {
        juce::AudioBuffer<float> audio(2, blockSize);
        for (int i = 0; i < blockSize; ++i) {
            const float sample = 0.15f * std::sin(static_cast<float>((block * blockSize + i) * 0.019));
            audio.setSample(0, i, sample);
            audio.setSample(1, i, sample * 0.8f);
        }
        if (block == 8)  { set(processor, "preDelaySync", 1.0f); set(processor, "syncDivision", 12.0f); }
        if (block == 16) { set(processor, "hqMode", 1.0f); }
        if (block == 24) { set(processor, "freeze", 1.0f); }
        if (block == 32) { processor.setCurrentProgram(0); }
        if (block == 48) { set(processor, "freeze", 0.0f); set(processor, "hqMode", 0.0f); }
        if (block == 56) { set(processor, "preDelaySync", 0.0f); set(processor, "preDelay", 0.1f); }
        processor.processBlock(audio, midi);
        if (!finiteAndSane(audio, peak, maxDelta)) return false;
    }
    // A deliberately generous regression guard: with a 0.15 input, a single
    // sample jump above 8 indicates a transition bug, not normal reverb audio.
    return peak < 16.0f && maxDelta < 8.0f;
}
}

int main() {
    struct Expected { int index; float milliseconds; };
    const Expected at120[] {{7, 500.f}, {4, 250.f}, {1, 125.f}, {5, 166.6667f}, {6, 375.f}, {11, 2000.f}, {12, 4000.f}};
    for (const auto& test : at120)
        if (!closeEnough(TempoSyncUtils::getMsFromBpm(120.f, test.index), test.milliseconds)) return 1;
    for (float bpm : {60.f, 90.f, 120.f, 180.f, 240.f})
        if (!std::isfinite(TempoSyncUtils::getMsFromBpm(bpm, 12))) return 2;
    if (!closeEnough(TempoSyncUtils::getMsFromBpm(0.f, 7), 500.f)
        || !closeEnough(TempoSyncUtils::getMsFromBpm(std::numeric_limits<float>::quiet_NaN(), 7), 500.f)) return 3;
    if (!closeEnough(TempoSyncUtils::getMsFromBpm(TempoSyncUtils::kMinimumSupportedBpm, 12), 8000.f)
        || !closeEnough(CloudGreyVerb::kPreDelayCapacitySeconds, 8.0f)
        || CloudGreyVerb::kPreDelayCapacitySeconds * 1000.f < TempoSyncUtils::getMsFromBpm(TempoSyncUtils::kMinimumSupportedBpm, 12)) return 4;

    for (int blockSize : {16, 32, 64, 128, 256, 512, 1024})
        if (!processTransitionMatrix(blockSize, 44100.0)) return 5;
    for (double sampleRate : {48000.0, 96000.0, 192000.0})
        if (!processTransitionMatrix(64, sampleRate)) return 6;

    CloudGreyVerbProcessor original;
    original.prepareToPlay(48000.0, 64);
    set(original, "hqMode", 1.f); set(original, "preDelaySync", 1.f); set(original, "sizeSync", 1.f);
    set(original, "syncDivision", 12.f); set(original, "sizeScale", 2.5f); set(original, "mix", .71f);
    set(original, "feedback", .79f); set(original, "stereoWidth", 1.6f); set(original, "reverseMix", .4f); set(original, "grainScan", .7f);
    juce::MemoryBlock state; original.getStateInformation(state);
    CloudGreyVerbProcessor restored; restored.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    for (const char* id : {"hqMode", "preDelaySync", "sizeSync", "syncDivision", "sizeScale", "mix", "feedback", "stereoWidth", "reverseMix", "grainScan"})
        if (!closeEnough(original.getVTS().getRawParameterValue(id)->load(), restored.getVTS().getRawParameterValue(id)->load(), 1.0e-5f)) return 7;
    std::cout << "Realtime transition, sync capacity, and state restore verified\n";
}
