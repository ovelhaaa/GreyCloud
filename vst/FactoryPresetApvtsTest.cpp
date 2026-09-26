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
bool samePersistedParameters(CloudGreyVerbProcessor& a, CloudGreyVerbProcessor& b) {
    for (auto* parameter : a.getParameters()) {
        auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(parameter);
        if (ranged == nullptr) return false;
        const auto* left = a.getVTS().getRawParameterValue(ranged->getParameterID());
        const auto* right = b.getVTS().getRawParameterValue(ranged->getParameterID());
        if (left == nullptr || right == nullptr || !equal(left->load(), right->load())) return false;
    }
    return true;
}
bool remainsUnknownOnlyNoOp(CloudGreyVerbProcessor& target,
                            CloudGreyVerbProcessor& before,
                            unsigned generationBefore) {
    return samePersistedParameters(target, before)
        && target.getCurrentProgram() == before.getCurrentProgram()
        && target.getPresetTransactionGenerationForTest() == generationBefore
        && !target.isPresetTransitionRequestedForTest()
        && !target.isPendingPresetTargetPublishedForTest()
        && target.isPresetTransitionIdleForTest();
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
    if (processor.getParameters().size() != 26) return 9;
    for (const auto& c : floats) {
        auto* p = processor.getVTS().getParameter(c.id);
        if (!p || p->getName(64) != c.name || !equal(p->getNormalisableRange().start,c.lo) || !equal(p->getNormalisableRange().end,c.hi) || !equal(p->convertFrom0to1(p->getDefaultValue()),c.def)) return 10;
    }
    const char* choices[] = { "-1 Oct", "+5th", "+1 Oct", "+1 Oct & 5th", "+2 Oct" };
    auto* shimmerRatioParam = processor.getVTS().getParameter("shimmerRatio");
    auto* divisionParam = processor.getVTS().getParameter("syncDivision");
    auto* shimmerRatio = dynamic_cast<juce::AudioParameterChoice*>(shimmerRatioParam);
    auto* division = dynamic_cast<juce::AudioParameterChoice*>(divisionParam);
    if (!shimmerRatio || !division || shimmerRatio->getName(64) != "Shimmer Ratio"
        || division->getName(64) != "Sync Division" || shimmerRatio->choices.size() != 5
        || division->choices.size() != TempoSyncUtils::kDivisionNames.size()
        || shimmerRatio->convertFrom0to1(shimmerRatioParam->getDefaultValue()) != 2.0f
        || division->convertFrom0to1(divisionParam->getDefaultValue()) != 7.0f) return 11;
    for (int i=0;i<5;++i) if (shimmerRatio->choices[i] != choices[i]) return 12;
    for (size_t i = 0; i < TempoSyncUtils::kDivisionNames.size(); ++i)
        if (division->choices[static_cast<int>(i)] != TempoSyncUtils::kDivisionNames[i]) return 20;
    struct BoolContract { const char* id; const char* name; bool def; };
    const BoolContract bools[] = { {"stereoCore", "Stereo Core", true}, {"hardFreeze", "Hard Freeze", false},
        {"hqMode", "HQ Mode", false}, {"preDelaySync", "Pre-Delay Sync", false}, {"sizeSync", "Size Sync", false} };
    for (const auto& c : bools) {
        auto* base = processor.getVTS().getParameter(c.id);
        auto* p = dynamic_cast<juce::AudioParameterBool*>(base);
        if (p == nullptr || p->getName(64) != c.name
            || (p->convertFrom0to1(base->getDefaultValue()) > .5f) != c.def) return 19;
    }
    if (processor.getVTS().getParameter("mix")->getText(.5f, 16) != "50 %"
        || processor.getVTS().getParameter("preDelay")->getText(.5f, 16) != "100 ms"
        || processor.getVTS().getParameter("preDelay")->getText(1.0f, 16) != "200 ms"
        || processor.getVTS().getParameter("inputGain")->getText(.5f, 16) != "0.0 dB"
        || processor.getVTS().getParameter("inputGain")->getText(0.0f, 16) != "-∞ dB"
        || processor.getVTS().getParameter("inputGain")->getText(1.0f, 16) != "6.0 dB"
        || processor.getVTS().getParameter("feedback")->getText(.5f, 16) != "47 %") return 13;
    // Slider double-click reset uses this conversion. In particular, a gain
    // default is plain 1.0, not its normalized .5 representation.
    for (const auto* id : { "inputGain", "outputGain", "stereoWidth", "feedback", "mix" }) {
        auto* p = processor.getVTS().getParameter(id);
        if (p == nullptr || !equal(p->convertFrom0to1(p->getDefaultValue()),
            juce::String(id) == "feedback" || juce::String(id) == "mix" ? .5f : 1.0f)) return 21;
    }
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
    for (const auto& bad : { juce::var("banana"), juce::var(true) }) {
        juce::NamedValueSet malformed; malformed.set("mix", bad);
        if (processor.importParameterSnapshot(malformed) || !equal(before, processor.getVTS().getRawParameterValue("mix")->load())) return 22;
    }
    for (const auto& bad : { juce::var(7.25), juce::var(13) }) {
        juce::NamedValueSet malformed; malformed.set("syncDivision", bad);
        if (processor.importParameterSnapshot(malformed)) return 23;
    }
    juce::NamedValueSet fractionalChoice; fractionalChoice.set("shimmerRatio", 2.25);
    if (processor.importParameterSnapshot(fractionalChoice)) return 31;
    juce::NamedValueSet fractionalBool; fractionalBool.set("hqMode", .5);
    if (processor.importParameterSnapshot(fractionalBool)) return 32;
    juce::NamedValueSet boolContract; boolContract.set("hqMode", true);
    if (!processor.importParameterSnapshot(boolContract) || !value(processor, "hqMode", 1.0f)) return 24;
    // APVTS serialization must retain canonical acoustic state, including HQ/sync flags.
    processor.setCurrentProgram(8);
    juce::MemoryBlock state; processor.getStateInformation(state);
    CloudGreyVerbProcessor restored; restored.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    if (!matches(restored, CloudGreyVerb::getFactoryPreset(8)) || restored.getCurrentProgram() != 8) return 3;
    // Preset base metadata survives an edited host session and clears again
    // only when the exact factory state is restored.
    processor.setCurrentProgram(7); juce::NamedValueSet edited; edited.set("mix", .123f);
    processor.importParameterSnapshot(edited); processor.getStateInformation(state);
    CloudGreyVerbProcessor editedRestored; editedRestored.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    if (editedRestored.getCurrentProgram() != 7 || !editedRestored.isCurrentPresetEdited()
        || !editedRestored.getCurrentPresetDisplayName().contains("BrightCloud")) return 25;
    juce::NamedValueSet exact; exact.set("mix", CloudGreyVerb::getFactoryPreset(7).dsp.mix);
    if (!editedRestored.importParameterSnapshot(exact) || editedRestored.isCurrentPresetEdited()) return 26;
    // Exercise the actual Nimbus v1 shape, not merely its final snapshot.
    CloudGreyVerbProcessor jsonSource; jsonSource.setCurrentProgram(8);
    const auto nimbus = jsonSource.serializePresetJson(); CloudGreyVerbProcessor jsonTarget;
    if (!jsonTarget.importPresetJson(nimbus) || !samePersistedParameters(jsonSource, jsonTarget)) return 27;
    auto legacy = nimbus.clone(); legacy.getDynamicObject()->setProperty("app", "GreyCloud");
    if (!jsonTarget.importPresetJson(legacy)) return 28;
    const auto jsonBefore = jsonTarget.getVTS().getRawParameterValue("mix")->load();
    for (const auto& text : { R"({"app":"Other","version":1,"presets":[{"params":{}}]})",
                               R"({"app":"Nimbus","version":2,"presets":[{"params":{}}]})",
                               R"({"app":"Nimbus","version":1,"presets":[{"params":"bad"}]})",
                               R"({"app":"Nimbus","version":1,"presets":"bad"})",
                               R"({"app":"Nimbus","version":1,"presets":[{"params":{"mix":2.0}}]})",
                               R"({"app":"Nimbus","version":1,"presets":[{"params":{"mix":"banana"}}]})",
                               R"({"app":"Nimbus","version":1,"presets":[{"params":{"syncDivision":7.5}}]})" })
        if (jsonTarget.importPresetJson(juce::JSON::parse(text))
            || !equal(jsonBefore, jsonTarget.getVTS().getRawParameterValue("mix")->load())) return 29;
    // A valid v1 snapshot containing only future parameters is a true no-op:
    // it must preserve the APVTS and must not publish an M4 restore target.
    juce::NamedValueSet unknownOnly; unknownOnly.set("futureParameter", 42.0f);
    CloudGreyVerbProcessor directUnknownTarget;
    CloudGreyVerbProcessor directUnknownBefore;
    const auto directGenerationBefore = directUnknownTarget.getPresetTransactionGenerationForTest();
    if (!directUnknownTarget.importParameterSnapshot(unknownOnly)) return 30;
    if (!remainsUnknownOnlyNoOp(directUnknownTarget, directUnknownBefore, directGenerationBefore)) return 33;
    CloudGreyVerbProcessor jsonUnknownTarget;
    CloudGreyVerbProcessor jsonUnknownBefore;
    const auto jsonGenerationBefore = jsonUnknownTarget.getPresetTransactionGenerationForTest();
    if (!jsonUnknownTarget.importPresetJson(juce::JSON::parse(R"({"app":"Nimbus","version":1,"presets":[{"params":{"futureParameter":42}}]})"))) return 35;
    if (!remainsUnknownOnlyNoOp(jsonUnknownTarget, jsonUnknownBefore, jsonGenerationBefore)) return 36;
    // An otherwise identical snapshot with a known ID must retain the normal
    // transactional import behavior; unknown IDs remain harmless metadata.
    CloudGreyVerbProcessor knownAndUnknownTarget;
    juce::NamedValueSet knownAndUnknown;
    knownAndUnknown.set("mix", 0.123f); knownAndUnknown.set("futureParameter", 42.0f);
    const auto knownGenerationBefore = knownAndUnknownTarget.getPresetTransactionGenerationForTest();
    if (!knownAndUnknownTarget.importParameterSnapshot(knownAndUnknown)
        || !value(knownAndUnknownTarget, "mix", 0.123f)
        || knownAndUnknownTarget.getPresetTransactionGenerationForTest() != knownGenerationBefore + 2
        || !knownAndUnknownTarget.isPresetTransitionRequestedForTest()
        || !knownAndUnknownTarget.isPendingPresetTargetPublishedForTest()
        || !knownAndUnknownTarget.isPresetTransitionIdleForTest()) return 34;
    std::cout << "FactoryPreset -> APVTS parity and persistence verified\n";
}
