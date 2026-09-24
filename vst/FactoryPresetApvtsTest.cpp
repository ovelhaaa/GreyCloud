#include "PluginProcessor.h"
#include <cmath>
#include <iostream>

namespace {
bool equal(float actual, float expected) { return std::abs(actual - expected) <= 1.0e-5f; }
bool value(CloudGreyVerbProcessor& processor, const char* id, float expected) {
    const auto* raw = processor.getVTS().getRawParameterValue(id);
    return raw != nullptr && equal(raw->load(), expected);
}
bool matches(CloudGreyVerbProcessor& processor, const CloudGreyVerb::FactoryPreset& factory) {
    const auto& p = factory.dsp;
    return value(processor,"mix",p.mix) && value(processor,"texture",p.texture)
        && value(processor,"freeze",p.freeze) && value(processor,"feedback",p.feedback)
        && value(processor,"size",p.size) && value(processor,"sizeScale",p.sizeScale)
        && value(processor,"diffusion",p.diffusion) && value(processor,"modDepth",p.modDepth)
        && value(processor,"modRate",p.modRate) && value(processor,"damping",p.damping)
        && value(processor,"lowDamping",p.lowDamping) && value(processor,"tone",p.tone)
        && value(processor,"shimmer",p.shimmer) && value(processor,"shimmerRatio",float(p.shimmerRatioIndex))
        && value(processor,"inputGain",p.inputGain) && value(processor,"outputGain",p.outputGain)
        && value(processor,"preDelay",p.preDelay) && value(processor,"stereoWidth",p.stereoWidth)
        && value(processor,"stereoCore",p.stereoCore ? 1.f : 0.f)
        && value(processor,"hardFreeze",p.hardFreeze ? 1.f : 0.f)
        && value(processor,"reverseMix",p.reverseMix) && value(processor,"grainScan",p.grainScan)
        && value(processor,"hqMode",factory.hqMode ? 1.f : 0.f)
        && value(processor,"preDelaySync",factory.preDelaySync ? 1.f : 0.f)
        && value(processor,"sizeSync",factory.sizeSync ? 1.f : 0.f)
        && value(processor,"syncDivision",float(factory.syncDivisionIndex));
}
}

int main() {
    CloudGreyVerbProcessor processor;
    if (processor.getCurrentProgram() != 0 || !matches(processor, CloudGreyVerb::getFactoryPreset(0)))
        return 1;
    for (size_t i = 0; i < CloudGreyVerb::factoryPresetCount(); ++i) {
        processor.setCurrentProgram(static_cast<int>(i));
        if (processor.getCurrentProgram() != static_cast<int>(i)
            || !matches(processor, CloudGreyVerb::getFactoryPreset(i)))
            return 2;
    }
    // APVTS serialization must retain canonical acoustic state, including HQ/sync flags.
    processor.setCurrentProgram(8);
    juce::MemoryBlock state; processor.getStateInformation(state);
    CloudGreyVerbProcessor restored; restored.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    if (!matches(restored, CloudGreyVerb::getFactoryPreset(8))) return 3;
    std::cout << "FactoryPreset -> APVTS parity and persistence verified\n";
}
