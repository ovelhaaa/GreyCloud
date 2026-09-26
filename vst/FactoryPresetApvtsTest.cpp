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
    // M5 public APVTS contract: persisted IDs, host names, ranges/defaults and
    // choice order are deliberately asserted here rather than inferred by UI.
    struct FloatContract { const char* id; const char* name; float lo, hi, def; };
    const FloatContract floats[] = {
        {"mix","Mix",0,1,.5f},{"texture","Texture",0,1,.5f},{"freeze","Freeze",0,1,0},{"feedback","Feedback",0,.94f,.5f},{"size","Size",0,1,.5f},{"sizeScale","Size Scale",1,CloudGreyVerb::kSizeMaxExtendedSeconds / CloudGreyVerb::kSizeMaxNormalSeconds,1},{"diffusion","Diffusion",0,1,.5f},{"modDepth","Mod Depth",0,1,.2f},{"modRate","Mod Rate",0,1,.2f},{"damping","Damping",0,1,.5f},{"lowDamping","Low Cut",0,1,.5f},{"tone","Tone",0,1,.5f},{"shimmer","Shimmer",0,1,0},{"inputGain","Input Gain",0,2,1},{"outputGain","Output Gain",0,2,1},{"preDelay","Pre-Delay",0,1,0},{"stereoWidth","Stereo Width",0,2,1},{"reverseMix","Reverse Mix",0,1,0},{"grainScan","Grain Scan",0,1,0}
    };
    if (processor.getVTS().getParameters().size() != 26) return 9;
    for (const auto& c : floats) {
        auto* p = processor.getVTS().getParameter(c.id);
        if (!p || p->getName(64) != c.name || !equal(p->getNormalisableRange().start,c.lo) || !equal(p->getNormalisableRange().end,c.hi) || !equal(p->convertFrom0to1(p->getDefaultValue()),c.def)) return 10;
    }
    const char* choices[] = { "-1 Oct", "+5th", "+1 Oct", "+1 Oct & 5th", "+2 Oct" };
    auto* shimmerRatio = dynamic_cast<juce::AudioParameterChoice*>(processor.getVTS().getParameter("shimmerRatio"));
    auto* division = dynamic_cast<juce::AudioParameterChoice*>(processor.getVTS().getParameter("syncDivision"));
    if (!shimmerRatio || !division || shimmerRatio->choices.size() != 5 || division->choices.size() != 13) return 11;
    for (int i=0;i<5;++i) if (shimmerRatio->choices[i] != choices[i]) return 12;
    const char* boolIds[] = { "stereoCore", "hardFreeze", "hqMode", "preDelaySync", "sizeSync" };
    for (const auto* id : boolIds)
        if (dynamic_cast<juce::AudioParameterBool*>(processor.getVTS().getParameter(id)) == nullptr) return 19;
    if (processor.getVTS().getParameter("mix")->getText(.5f, 16) != "50 %"
        || processor.getVTS().getParameter("preDelay")->getText(.5f, 16) != "100 ms"
        || processor.getVTS().getParameter("preDelay")->getText(1.0f, 16) != "200 ms"
        || processor.getVTS().getParameter("inputGain")->getText(.5f, 16) != "0.0 dB"
        || processor.getVTS().getParameter("inputGain")->getText(0.0f, 16) != "-∞ dB") return 13;
    if (processor.getCurrentProgram() != 0 || !matches(processor, CloudGreyVerb::getFactoryPreset(0)))
        return 1;
    for (size_t i = 0; i < CloudGreyVerb::factoryPresetCount(); ++i) {
        processor.setCurrentProgram(static_cast<int>(i));
        if (processor.getCurrentProgram() != static_cast<int>(i)
            || !matches(processor, CloudGreyVerb::getFactoryPreset(i)))
            return 2;
    }
    processor.setCurrentProgram(7);
    if (processor.isCurrentPresetEdited()) return 14;
    juce::NamedValueSet edit; edit.set("mix", 0.123f);
    if (!processor.importParameterSnapshot(edit) || !processor.isCurrentPresetEdited()) return 15;
    juce::NamedValueSet restore; restore.set("mix", CloudGreyVerb::getFactoryPreset(7).dsp.mix);
    if (!processor.importParameterSnapshot(restore) || processor.isCurrentPresetEdited()) return 16;
    const auto before = processor.getVTS().getRawParameterValue("mix")->load();
    juce::NamedValueSet invalid; invalid.set("mix", 9.0f); invalid.set("texture", 0.0f);
    if (processor.importParameterSnapshot(invalid) || !equal(before, processor.getVTS().getRawParameterValue("mix")->load())) return 17;
    juce::NamedValueSet unknown; unknown.set("futureParameter", 42.0f);
    if (!processor.importParameterSnapshot(unknown)) return 18;
    // APVTS serialization must retain canonical acoustic state, including HQ/sync flags.
    processor.setCurrentProgram(8);
    juce::MemoryBlock state; processor.getStateInformation(state);
    CloudGreyVerbProcessor restored; restored.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    if (!matches(restored, CloudGreyVerb::getFactoryPreset(8))) return 3;
    std::cout << "FactoryPreset -> APVTS parity and persistence verified\n";
}
