#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "TempoSyncUtils.h"

#include <cmath>

namespace {
// The sole FactoryPreset -> APVTS mapping.  Keep this list exhaustive: it is
// exercised by the JUCE-side parity test, including the non-DSP persisted flags.
void applyFactoryPresetToParameters(juce::AudioProcessorValueTreeState& parameters,
                                    const CloudGreyVerb::FactoryPreset& preset)
{
    const auto& p = preset.dsp;
    const auto set = [&](const char* id, float value) {
        if (auto* parameter = parameters.getParameter(id))
            parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
    };
    set("mix", p.mix); set("texture", p.texture); set("freeze", p.freeze);
    set("feedback", p.feedback); set("size", p.size); set("sizeScale", p.sizeScale);
    set("diffusion", p.diffusion); set("modDepth", p.modDepth); set("modRate", p.modRate);
    set("damping", p.damping); set("lowDamping", p.lowDamping); set("tone", p.tone);
    set("shimmer", p.shimmer); set("shimmerRatio", static_cast<float>(p.shimmerRatioIndex));
    set("inputGain", p.inputGain); set("outputGain", p.outputGain); set("preDelay", p.preDelay);
    set("stereoWidth", p.stereoWidth); set("stereoCore", p.stereoCore ? 1.0f : 0.0f);
    set("hardFreeze", p.hardFreeze ? 1.0f : 0.0f); set("reverseMix", p.reverseMix);
    set("grainScan", p.grainScan); set("hqMode", preset.hqMode ? 1.0f : 0.0f);
    set("preDelaySync", preset.preDelaySync ? 1.0f : 0.0f);
    set("sizeSync", preset.sizeSync ? 1.0f : 0.0f);
    set("syncDivision", static_cast<float>(preset.syncDivisionIndex));
}
}

// Factory function to create parameters
juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    const auto percentText = [] (float v, int) { return juce::String (juce::roundToInt (v * 100.0f)) + " %"; };
    const auto msText = [] (float v, int) { return juce::String (juce::roundToInt (v * 200.0f)) + " ms"; };
    const auto gainText = [] (float v, int) { return v <= 0.00001f ? juce::String::fromUTF8 ("-\xE2\x88\x9E dB") : juce::String (20.0f * std::log10 (v), 1) + " dB"; };
    const auto pct = [&] (const char* id, const char* name, float lo, float hi, float def) {
        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID { id, 1 }, name, juce::NormalisableRange<float> (lo, hi), def,
            juce::AudioParameterFloatAttributes().withStringFromValueFunction(percentText)));
    };
    pct("mix", "Mix", 0.0f, 1.0f, 0.5f);
    pct("texture", "Texture", 0.0f, 1.0f, 0.5f);
    params.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{"freeze", 1}, "Freeze", 0.0f, 1.0f, 0.0f));
    pct("feedback", "Feedback", 0.0f, 0.94f, 0.5f);
    params.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{"size", 1}, "Size", 0.0f, 1.0f, 0.5f));
    // Persisted policy for exceptional Greyhole-scale spaces; it is not a UI control.
    params.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{"sizeScale", 1}, "Size Scale", 1.0f, CloudGreyVerb::kSizeMaxExtendedSeconds / CloudGreyVerb::kSizeMaxNormalSeconds, 1.0f));
    pct("diffusion", "Diffusion", 0.0f, 1.0f, 0.5f);
    pct("modDepth", "Mod Depth", 0.0f, 1.0f, 0.2f);
    params.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{"modRate", 1}, "Mod Rate", 0.0f, 1.0f, 0.2f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{"damping", 1}, "Damping", 0.0f, 1.0f, 0.5f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{"lowDamping", 1}, "Low Cut", 0.0f, 1.0f, 0.5f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{"tone", 1}, "Tone", 0.0f, 1.0f, 0.5f));
    pct("shimmer", "Shimmer", 0.0f, 1.0f, 0.0f);
    const auto gainAttributes = juce::AudioParameterFloatAttributes().withStringFromValueFunction(gainText);
    params.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{"inputGain", 1}, "Input Gain", juce::NormalisableRange<float>(0.0f, 2.0f), 1.0f, gainAttributes));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{"outputGain", 1}, "Output Gain", juce::NormalisableRange<float>(0.0f, 2.0f), 1.0f, gainAttributes));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{"preDelay", 1}, "Pre-Delay", juce::NormalisableRange<float>(0.0f, 1.0f), 0.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction(msText)));
    pct("stereoWidth", "Stereo Width", 0.0f, 2.0f, 1.0f);

    juce::StringArray shimmerChoices = { "-1 Oct", "+5th", "+1 Oct", "+1 Oct & 5th", "+2 Oct" };
    params.push_back(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{"shimmerRatio", 1}, "Shimmer Ratio", shimmerChoices, 2));
    
    params.push_back(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"stereoCore", 1}, "Stereo Core", true));
    params.push_back(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"hardFreeze", 1}, "Hard Freeze", false));
    params.push_back(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"hqMode", 1}, "HQ Mode", false));

    params.push_back(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"preDelaySync", 1}, "Pre-Delay Sync", false));
    params.push_back(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"sizeSync", 1}, "Size Sync", false));
    
    juce::StringArray syncChoices;
    for (const auto* name : TempoSyncUtils::kDivisionNames)
        syncChoices.add(name);
    params.push_back(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{"syncDivision", 1}, "Sync Division", syncChoices, 7)); // Default "1/4"

    params.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{"reverseMix", 1}, "Reverse Mix", 0.0f, 1.0f, 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{"grainScan", 1}, "Grain Scan", 0.0f, 1.0f, 0.0f));

    return { params.begin(), params.end() };
}

CloudGreyVerbProcessor::CloudGreyVerbProcessor()
    : AudioProcessor (BusesProperties()
                      .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                      .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      parameters (*this, nullptr, juce::Identifier ("CloudGreyVerbVTS"), createParameterLayout())
{
    currentPresetIndex = 0;
    // A new instance must not advertise SmallCloudRoom while holding generic
    // APVTS defaults. This happens before a host can process audio/automation.
    applyFactoryPresetToParameters(parameters, CloudGreyVerb::getFactoryPreset(0));
}

CloudGreyVerbProcessor::~CloudGreyVerbProcessor() = default;

const juce::String CloudGreyVerbProcessor::getName() const { return JucePlugin_Name; }
bool CloudGreyVerbProcessor::acceptsMidi() const { return false; }
bool CloudGreyVerbProcessor::producesMidi() const { return false; }
bool CloudGreyVerbProcessor::isMidiEffect() const { return false; }
// Conservative finite report for normal/long factory tails. Freeze can be
// indefinite by design, but hosts need a useful non-zero scheduling value.
double CloudGreyVerbProcessor::getTailLengthSeconds() const { return 30.0; }
int CloudGreyVerbProcessor::getNumPrograms() { return static_cast<int>(CloudGreyVerb::factoryPresetCount()); }
int CloudGreyVerbProcessor::getCurrentProgram() { return currentPresetIndex; }

void CloudGreyVerbProcessor::setCurrentProgram (int index)
{
    if (index >= 0 && index < getNumPrograms())
    {
        currentPresetIndex = index;
        const auto& preset = CloudGreyVerb::getFactoryPreset(static_cast<size_t>(index));
        presetTransactionGeneration.fetch_add (1, std::memory_order_acq_rel); // odd: APVTS transaction open
        applyFactoryPresetToParameters(parameters, preset);
        // Publish the request after the complete APVTS transaction, so the
        // audio thread never starts a transition against half a preset.
        publishPresetTarget(preset);
        presetTransactionGeneration.fetch_add (1, std::memory_order_release); // even: complete snapshot published
    }
}

const juce::String CloudGreyVerbProcessor::getProgramName (int index) 
{ 
    if (index >= 0 && index < getNumPrograms())
        return CloudGreyVerb::getFactoryPreset(static_cast<size_t>(index)).name;
    return {}; 
}

void CloudGreyVerbProcessor::changeProgramName (int index, const juce::String& newName) 
{
    // Factory programs are immutable by design: their names and acoustic
    // state must remain the shared core/VST/benchmark truth.
    juce::ignoreUnused (index, newName);
}

void CloudGreyVerbProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    presetTransitionRequested.store (false, std::memory_order_release);
    presetTransitionStage.store (static_cast<int> (PresetTransitionStage::idle), std::memory_order_release);
    presetTransitionSamplesRemaining = 0;
    presetTransitionSamplesTotal = 0;

    // Exact profile/sample-rate-derived pools. This is deliberately not a
    // desktop-sized magic allocation for H5/H7/WASM builds.
    const size_t requiredFloats = CloudGreyVerb::requiredMemoryFloats(static_cast<float>(sampleRate));
    dspMemoryNormal.resize(requiredFloats, 0.0f);
    
    // For HQ mode (2x oversampling), we need to handle 2x sample rate without halving max delay times.
    const size_t requiredFloatsHQ = CloudGreyVerb::requiredMemoryFloats(static_cast<float>(sampleRate * 2.0));
    dspMemoryHQ.resize(requiredFloatsHQ, 0.0f);
    
    dspCoreNormal.init(static_cast<float>(sampleRate), dspMemoryNormal.data(), requiredFloats);
    dspCoreHQ.init(static_cast<float>(sampleRate * 2.0), dspMemoryHQ.data(), requiredFloatsHQ);
    coresReady = dspCoreNormal.isInitialized() && dspCoreHQ.isInitialized();
    if (!coresReady) {
        juce::Logger::writeToLog("Nimbus DSP initialization failed: unsupported sample rate or insufficient fixed DSP memory.");
        jassertfalse;
    }
    
    oversampling = std::make_unique<juce::dsp::Oversampling<float>> (2, 1, juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple, true);
    oversampling->initProcessing (samplesPerBlock);
    
    // Latency reporting
    currentLatencySamples = oversampling->getLatencyInSamples();
    setLatencySamples(static_cast<int>(std::round(currentLatencySamples)));
    
    // Set up delay line for PDC
    juce::dsp::ProcessSpec spec;
    spec.sampleRate = sampleRate;
    spec.maximumBlockSize = samplesPerBlock;
    spec.numChannels = 1;
    
    latencyCompensationL.prepare(spec);
    latencyCompensationR.prepare(spec);
    transitionDryDelayL.prepare(spec);
    transitionDryDelayR.prepare(spec);
    latencyCompensationL.setDelay(currentLatencySamples);
    latencyCompensationR.setDelay(currentLatencySamples);
    transitionDryDelayL.setDelay(currentLatencySamples);
    transitionDryDelayR.setDelay(currentLatencySamples);
    transitionDryBuffer.setSize(2, samplesPerBlock, false, false, true);
    hasProcessedAudio.store (false, std::memory_order_release);
    const auto initialTarget = makeDspSnapshotFromParameters();
    currentDspParams = resolveRuntimeParams (initialTarget);
    currentDspHqMode = initialTarget.hqMode;
    currentPreDelaySync = initialTarget.preDelaySync;
    currentSizeSync = initialTarget.sizeSync;
    currentSyncDivision = initialTarget.syncDivision;
}

void CloudGreyVerbProcessor::requestPresetTransition()
{
    presetTransitionRequested.store (true, std::memory_order_release);
}

bool CloudGreyVerbProcessor::importParameterSnapshot (const juce::NamedValueSet& values)
{
    // First pass is deliberately side-effect free. Unknown IDs are retained as
    // forward-compatible metadata; every known value must be finite and in its
    // public range, so no malformed file can leave a half-applied preset.
    size_t knownCount = 0;
    for (const auto& property : values)
    {
        if (auto* parameter = parameters.getParameter (property.name.toString()))
        {
            ++knownCount;
            float value = 0.0f;
            if (! validateSnapshotValue (*parameter, property.value, value))
                return false;
        }
    }

    // A forward-compatible snapshot can contain only parameters introduced by
    // a newer version. It is valid, but must not create a no-op M4 transition
    // (or otherwise mutate this instance's APVTS/transaction state).
    if (knownCount == 0)
        return true;

    presetTransactionGeneration.fetch_add (1, std::memory_order_acq_rel);
    for (const auto& property : values)
        if (auto* parameter = parameters.getParameter (property.name.toString()))
        {
            float value = 0.0f;
            const auto valid = validateSnapshotValue (*parameter, property.value, value);
            jassert (valid); // First pass above guarantees this remains true.
            parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
        }
    const auto target = makeDspSnapshotFromParameters();
    publishRestoreTarget (target);
    requestPresetTransition();
    presetTransactionGeneration.fetch_add (1, std::memory_order_release);
    return true;
}

bool CloudGreyVerbProcessor::validateSnapshotValue (juce::RangedAudioParameter& parameter,
                                                    const juce::var& source,
                                                    float& plainValue) const
{
    // juce::var silently coerces strings ("banana" becomes zero), which is
    // unsafe for an all-or-nothing import. Accept only JSON numbers, except
    // that boolean parameters also accept native JSON true/false.
    const auto numeric = source.isInt() || source.isInt64() || source.isDouble();
    const auto isBool = dynamic_cast<juce::AudioParameterBool*> (&parameter) != nullptr;
    const auto isChoice = dynamic_cast<juce::AudioParameterChoice*> (&parameter) != nullptr;

    if (isBool && source.isBool())
        plainValue = static_cast<bool> (source) ? 1.0f : 0.0f;
    else if (numeric)
        plainValue = static_cast<float> (static_cast<double> (source));
    else
        return false;

    if (! std::isfinite (plainValue)
        || plainValue < parameter.getNormalisableRange().start
        || plainValue > parameter.getNormalisableRange().end)
        return false;

    // Choice values are public indices, not continuously scalable numbers.
    if (isChoice && plainValue != std::round (plainValue))
        return false;

    // Numeric JSON remains supported for compatibility with earlier Nimbus
    // files, but booleans must still be represented unambiguously.
    if (isBool && plainValue != 0.0f && plainValue != 1.0f)
        return false;

    return true;
}

juce::var CloudGreyVerbProcessor::serializePresetJson() const
{
    juce::DynamicObject::Ptr preset = new juce::DynamicObject();
    preset->setProperty ("name", getCurrentPresetDisplayName());
    juce::DynamicObject::Ptr snapshot = new juce::DynamicObject();

    // APVTS does not own an enumeration API in JUCE 8. The processor owns the
    // actual parameter list, and raw APVTS values are already plain values.
    for (auto* parameter : getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (parameter))
            if (auto* raw = parameters.getRawParameterValue (ranged->getParameterID()))
                snapshot->setProperty (ranged->getParameterID(), raw->load());

    preset->setProperty ("params", juce::var (snapshot.get()));
    juce::Array<juce::var> presets;
    presets.add (juce::var (preset.get()));
    juce::DynamicObject::Ptr root = new juce::DynamicObject();
    root->setProperty ("app", "Nimbus");
    root->setProperty ("version", 1);
    root->setProperty ("presets", juce::var (presets));
    return juce::var (root.get());
}

bool CloudGreyVerbProcessor::importPresetJson (const juce::var& root, int presetIndex)
{
    if (! root.isObject()) return false;
    auto* object = root.getDynamicObject();
    if (object == nullptr) return false;
    const auto app = object->getProperty ("app");
    const auto version = object->getProperty ("version");
    if (! app.isString() || (app.toString() != "Nimbus" && app.toString() != "GreyCloud")
        || !(version.isInt() || version.isInt64() || version.isDouble())
        || static_cast<double> (version) != 1.0)
        return false;

    const auto presetsVar = object->getProperty ("presets");
    auto* presets = presetsVar.getArray();
    if (presets == nullptr || presetIndex < 0 || presetIndex >= presets->size()) return false;
    const auto preset = presets->getReference (presetIndex);
    if (! preset.isObject()) return false;
    const auto params = preset.getDynamicObject()->getProperty ("params");
    if (! params.isObject()) return false;

    juce::NamedValueSet snapshot;
    for (const auto& property : params.getDynamicObject()->getProperties())
        snapshot.set (property.name, property.value);
    return importParameterSnapshot (snapshot);
}

bool CloudGreyVerbProcessor::isCurrentPresetEdited() const
{
    if (currentPresetIndex < 0 || currentPresetIndex >= static_cast<int> (CloudGreyVerb::factoryPresetCount())) return true;
    const auto& f = CloudGreyVerb::getFactoryPreset (static_cast<size_t> (currentPresetIndex));
    const auto target = makeDspSnapshotFromParameters();
    const auto& p = target.params;
    const auto same = [] (float a, float b) { return std::abs (a - b) < 1.0e-6f; };
    return !(same(p.mix,f.dsp.mix) && same(p.texture,f.dsp.texture) && same(p.freeze,f.dsp.freeze)
        && same(p.feedback,f.dsp.feedback) && same(p.size,f.dsp.size) && same(p.sizeScale,f.dsp.sizeScale)
        && same(p.diffusion,f.dsp.diffusion) && same(p.modDepth,f.dsp.modDepth) && same(p.modRate,f.dsp.modRate)
        && same(p.damping,f.dsp.damping) && same(p.lowDamping,f.dsp.lowDamping) && same(p.tone,f.dsp.tone)
        && same(p.shimmer,f.dsp.shimmer) && p.shimmerRatioIndex == f.dsp.shimmerRatioIndex
        && same(p.inputGain,f.dsp.inputGain) && same(p.outputGain,f.dsp.outputGain) && same(p.preDelay,f.dsp.preDelay)
        && same(p.stereoWidth,f.dsp.stereoWidth) && p.stereoCore == f.dsp.stereoCore && p.hardFreeze == f.dsp.hardFreeze
        && same(p.reverseMix,f.dsp.reverseMix) && same(p.grainScan,f.dsp.grainScan) && target.hqMode == f.hqMode
        && target.preDelaySync == f.preDelaySync && target.sizeSync == f.sizeSync && target.syncDivision == f.syncDivisionIndex);
}

juce::String CloudGreyVerbProcessor::getCurrentPresetDisplayName() const
{
    const auto name = (currentPresetIndex >= 0 && currentPresetIndex < static_cast<int> (CloudGreyVerb::factoryPresetCount()))
        ? juce::String (CloudGreyVerb::getFactoryPreset (static_cast<size_t> (currentPresetIndex)).name) : juce::String ("Custom");
    return isCurrentPresetEdited() ? name + juce::String::fromUTF8 (" \xE2\x80\xA2 Edited") : name;
}

void CloudGreyVerbProcessor::publishPresetTarget (const CloudGreyVerb::FactoryPreset& preset)
{
    const int next = 1 - pendingPresetTargetIndex.load (std::memory_order_relaxed);
    auto target = preset.dsp;
    target.clipOutput = false;
    pendingPresetTargets[next] = { target, preset.hqMode, preset.preDelaySync,
                                   preset.sizeSync, preset.syncDivisionIndex };
    pendingPresetTargetIndex.store (next, std::memory_order_release);
    pendingPresetTargetPublished.store (true, std::memory_order_release);
    requestPresetTransition();
}

CloudGreyVerbProcessor::TransitionTarget CloudGreyVerbProcessor::makeDspSnapshotFromParameters() const
{
    TransitionTarget target;
    auto& p = target.params;
    p.mix = parameters.getRawParameterValue("mix")->load();
    p.texture = parameters.getRawParameterValue("texture")->load();
    p.freeze = parameters.getRawParameterValue("freeze")->load();
    p.feedback = parameters.getRawParameterValue("feedback")->load();
    p.size = parameters.getRawParameterValue("size")->load();
    p.sizeScale = parameters.getRawParameterValue("sizeScale")->load();
    p.diffusion = parameters.getRawParameterValue("diffusion")->load();
    p.modDepth = parameters.getRawParameterValue("modDepth")->load();
    p.modRate = parameters.getRawParameterValue("modRate")->load();
    p.damping = parameters.getRawParameterValue("damping")->load();
    p.lowDamping = parameters.getRawParameterValue("lowDamping")->load();
    p.tone = parameters.getRawParameterValue("tone")->load();
    p.shimmer = parameters.getRawParameterValue("shimmer")->load();
    p.shimmerRatioIndex = static_cast<int> (parameters.getRawParameterValue("shimmerRatio")->load());
    p.inputGain = parameters.getRawParameterValue("inputGain")->load();
    p.outputGain = parameters.getRawParameterValue("outputGain")->load();
    p.preDelay = parameters.getRawParameterValue("preDelay")->load();
    p.preDelaySeconds = -1.0f;
    p.stereoWidth = parameters.getRawParameterValue("stereoWidth")->load();
    p.stereoCore = parameters.getRawParameterValue("stereoCore")->load() > 0.5f;
    p.hardFreeze = parameters.getRawParameterValue("hardFreeze")->load() > 0.5f;
    p.reverseMix = parameters.getRawParameterValue("reverseMix")->load();
    p.grainScan = parameters.getRawParameterValue("grainScan")->load();
    p.clipOutput = false;
    target.hqMode = parameters.getRawParameterValue("hqMode")->load() > 0.5f;
    target.preDelaySync = parameters.getRawParameterValue("preDelaySync")->load() > 0.5f;
    target.sizeSync = parameters.getRawParameterValue("sizeSync")->load() > 0.5f;
    target.syncDivision = static_cast<int>(parameters.getRawParameterValue("syncDivision")->load());
    return target;
}

CloudGreyVerb::Params CloudGreyVerbProcessor::resolveRuntimeParams (const TransitionTarget& target) const
{
    auto p = target.params;
    if (target.preDelaySync || target.sizeSync) {
        const auto bpm = displayBpm.load (std::memory_order_relaxed);
        const float syncMs = TempoSyncUtils::getMsFromBpm(bpm, target.syncDivision);
        if (target.preDelaySync) p.preDelaySeconds = syncMs / 1000.0f;
        if (target.sizeSync) p.size = CloudGreyVerb::secondsToSize(syncMs / 1000.0f, p.sizeScale);
    }
    return p;
}

void CloudGreyVerbProcessor::publishRestoreTarget (const TransitionTarget& target)
{
    const int next = 1 - pendingPresetTargetIndex.load (std::memory_order_relaxed);
    pendingPresetTargets[next] = target;
    pendingPresetTargetIndex.store (next, std::memory_order_release);
    pendingPresetTargetPublished.store (true, std::memory_order_release);
}

int CloudGreyVerbProcessor::getPresetTransitionLengthInSamples (double seconds) const
{
    return juce::jmax (1, juce::roundToInt (currentSampleRate * seconds));
}

void CloudGreyVerbProcessor::resetDspStateForTransition (bool targetHq)
{
    if (! coresReady) return;
    // Only wet state changes. Dry/PDC input history is intentionally retained
    // so latency and dry continuity survive preset and HQ transitions.
    if (targetHq) {
        dspCoreHQ.reset();
        if (oversampling != nullptr) oversampling->reset();
    } else {
        dspCoreNormal.reset();
        latencyCompensationL.reset();
        latencyCompensationR.reset();
    }
}

void CloudGreyVerbProcessor::applyPresetTransition (juce::AudioBuffer<float>& buffer)
{
    auto stage = static_cast<PresetTransitionStage> (presetTransitionStage.load (std::memory_order_acquire));
    if (stage == PresetTransitionStage::idle || buffer.getNumSamples() == 0)
        return;

    if (presetTransitionSamplesRemaining <= 0)
    {
        const auto fadeSeconds = (stage == PresetTransitionStage::fadeOut) ? 0.012 : 0.008;
        presetTransitionSamplesTotal = getPresetTransitionLengthInSamples (fadeSeconds);
        presetTransitionSamplesRemaining = presetTransitionSamplesTotal;
    }

    bool shouldResetDsp = false;
    auto numSamples = buffer.getNumSamples();
    auto numChannels = buffer.getNumChannels();

    for (int sample = 0; sample < numSamples; ++sample)
    {
        float gain = 1.0f;

        if (stage == PresetTransitionStage::fadeOut)
        {
            gain = static_cast<float> (presetTransitionSamplesRemaining)
                 / static_cast<float> (presetTransitionSamplesTotal);
        }
        else if (stage == PresetTransitionStage::fadeIn)
        {
            gain = 1.0f - (static_cast<float> (presetTransitionSamplesRemaining)
                         / static_cast<float> (presetTransitionSamplesTotal));
        }

        // Fade wet only. The dry reference has the same fixed latency policy
        // as the processor output, so a preset reset never mutes dry audio.
        const float dryGain = currentDspParams.inputGain
                            * std::sqrt (1.0f - currentDspParams.mix)
                            * currentDspParams.outputGain;
        for (int channel = 0; channel < numChannels; ++channel)
        {
            const float dry = transitionDryBuffer.getReadPointer(channel)[sample] * dryGain;
            const float wet = buffer.getWritePointer(channel)[sample] - dry;
            buffer.getWritePointer(channel)[sample] = dry + wet * gain;
        }

        --presetTransitionSamplesRemaining;

        if (presetTransitionSamplesRemaining <= 0)
        {
            if (stage == PresetTransitionStage::fadeOut)
            {
                shouldResetDsp = true;
                // Finish this block at zero wet, while preserving the delayed
                // dry path. The old code zeroed the complete output here.
                for (int restSample = sample + 1; restSample < numSamples; ++restSample)
                    for (int channel = 0; channel < numChannels; ++channel)
                        buffer.getWritePointer (channel)[restSample]
                            = transitionDryBuffer.getReadPointer(channel)[restSample] * dryGain;
            }
            else
            {
                presetTransitionStage.store (static_cast<int> (PresetTransitionStage::idle), std::memory_order_release);
            }

            break;
        }
    }

    if (shouldResetDsp)
    {
        resetDspStateForTransition (transitionTarget.hqMode);
        currentDspParams = resolveRuntimeParams (transitionTarget);
        currentDspHqMode = transitionTarget.hqMode;
        currentPreDelaySync = transitionTarget.preDelaySync;
        currentSizeSync = transitionTarget.sizeSync;
        currentSyncDivision = transitionTarget.syncDivision;
        presetTransitionSamplesTotal = getPresetTransitionLengthInSamples (0.008);
        presetTransitionSamplesRemaining = presetTransitionSamplesTotal;
        presetTransitionStage.store (static_cast<int> (PresetTransitionStage::fadeIn), std::memory_order_release);
    }
}

void CloudGreyVerbProcessor::releaseResources()
{
}

bool CloudGreyVerbProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
     && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;

    return true;
}

void CloudGreyVerbProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    auto totalNumInputChannels  = getTotalNumInputChannels();
    auto totalNumOutputChannels = getTotalNumOutputChannels();

    // The UI reads this atomically, but the host playhead is only queried from
    // the audio callback. Publish a sanitized value for every block, whether
    // or not either tempo-sync target is currently enabled.
    float hostBpm = TempoSyncUtils::kFallbackBpm;
    if (auto* playHead = getPlayHead())
        if (auto position = playHead->getPosition())
            if (position->getBpm().hasValue())
                hostBpm = static_cast<float> (*position->getBpm());
    displayBpm.store (TempoSyncUtils::sanitizeBpm (hostBpm), std::memory_order_relaxed);

    const bool presetRequested = presetTransitionRequested.exchange (false, std::memory_order_acq_rel);

    for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear (i, 0, buffer.getNumSamples());

    // prepareToPlay cannot return an error to a host. The failure is logged and
    // asserted above; bypass deterministically instead of selecting a bad core.
    if (!coresReady)
        return;
    hasProcessedAudio.store (true, std::memory_order_release);

    const auto transactionBefore = presetTransactionGeneration.load (std::memory_order_acquire);
    // This is the sole APVTS -> DSP mapping; restore and live processing must
    // consume exactly the same persisted state.
    auto liveTarget = makeDspSnapshotFromParameters();

    const auto transactionAfter = presetTransactionGeneration.load (std::memory_order_acquire);
    if ((transactionBefore & 1u) != 0 || transactionBefore != transactionAfter)
    {
        // A program update overlapped this callback: keep the last complete
        // state until its published target begins the transition.
        liveTarget = { currentDspParams, currentDspHqMode, currentPreDelaySync,
                       currentSizeSync, currentSyncDivision };
    }
    const auto liveParams = resolveRuntimeParams (liveTarget);

    auto transitionStage = static_cast<PresetTransitionStage> (presetTransitionStage.load (std::memory_order_acquire));
    if (presetRequested)
    {
        transitionTarget = pendingPresetTargetPublished.exchange (false, std::memory_order_acq_rel)
            ? pendingPresetTargets[pendingPresetTargetIndex.load (std::memory_order_acquire)]
            : liveTarget;
        presetTransitionSamplesRemaining = 0;
        presetTransitionSamplesTotal = 0;
        presetTransitionStage.store (static_cast<int> (PresetTransitionStage::fadeOut), std::memory_order_release);
        transitionStage = PresetTransitionStage::fadeOut;
    }
    else if (transitionStage == PresetTransitionStage::idle && liveTarget.hqMode != currentDspHqMode)
    {
        transitionTarget = liveTarget;
        presetTransitionSamplesRemaining = 0;
        presetTransitionSamplesTotal = 0;
        presetTransitionStage.store (static_cast<int> (PresetTransitionStage::fadeOut), std::memory_order_release);
        transitionStage = PresetTransitionStage::fadeOut;
    }
    const bool holdPreviousDspState = transitionStage == PresetTransitionStage::fadeOut;
    const bool useTransitionTarget = transitionStage == PresetTransitionStage::fadeIn;
    const auto paramsToProcess = holdPreviousDspState ? currentDspParams
                               : (useTransitionTarget ? resolveRuntimeParams (transitionTarget) : liveParams);
    const auto hqModeToProcess = holdPreviousDspState ? currentDspHqMode
                               : (useTransitionTarget ? transitionTarget.hqMode : liveTarget.hqMode);

    if (! holdPreviousDspState && ! useTransitionTarget)
    {
        currentDspParams = liveParams;
        currentDspHqMode = liveTarget.hqMode;
        currentPreDelaySync = liveTarget.preDelaySync;
        currentSizeSync = liveTarget.sizeSync;
        currentSyncDivision = liveTarget.syncDivision;
    }

    // Keep a latency-matched dry reference for the wet-only preset envelope.
    // This is preallocated in prepareToPlay and has no callback allocation.
    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        const float inL = buffer.getReadPointer(0)[i];
        const float inR = totalNumOutputChannels > 1 ? buffer.getReadPointer(1)[i] : inL;
        transitionDryBuffer.setSample(0, i, transitionDryDelayL.popSample(0));
        transitionDryDelayL.pushSample(0, inL);
        if (totalNumOutputChannels > 1) {
            transitionDryBuffer.setSample(1, i, transitionDryDelayR.popSample(0));
            transitionDryDelayR.pushSample(0, inR);
        }
    }
    lastRuntimePreDelaySeconds.store (paramsToProcess.preDelaySeconds, std::memory_order_relaxed);

    if (hqModeToProcess) {
        dspCoreHQ.setParams(paramsToProcess);
    } else {
        dspCoreNormal.setParams(paramsToProcess);
    }

    if (hqModeToProcess) {
        juce::dsp::AudioBlock<float> block (buffer);
        juce::dsp::AudioBlock<float> osBlock = oversampling->processSamplesUp (block);
        
        float* channelL = osBlock.getChannelPointer(0);
        float* channelR = (osBlock.getNumChannels() > 1) ? osBlock.getChannelPointer(1) : nullptr;
        
        int numOsSamples = static_cast<int>(osBlock.getNumSamples());
        for (int i = 0; i < numOsSamples; ++i) {
            float inL = channelL[i];
            float inR = channelR ? channelR[i] : inL;
            
            float outL = 0.0f;
            float outR = 0.0f;
            
            dspCoreHQ.processSample(inL, inR, outL, outR);
            
            channelL[i] = outL;
            if (channelR) channelR[i] = outR;
        }
        
        oversampling->processSamplesDown (block);
    } else {
        int numSamples = buffer.getNumSamples();
        float* channelL = buffer.getWritePointer(0);
        float* channelR = (totalNumOutputChannels > 1) ? buffer.getWritePointer(1) : nullptr;

        for (int i = 0; i < numSamples; ++i) {
            float inL = channelL[i];
            float inR = channelR ? channelR[i] : inL;
            
            float outL = 0.0f;
            float outR = 0.0f;
            
            dspCoreNormal.processSample(inL, inR, outL, outR);
            
            float delayedL = latencyCompensationL.popSample(0);
            latencyCompensationL.pushSample(0, outL);
            outL = delayedL;
            
            if (channelR) {
                float delayedR = latencyCompensationR.popSample(0);
                latencyCompensationR.pushSample(0, outR);
                outR = delayedR;
            }
            
            channelL[i] = outL;
            if (channelR) channelR[i] = outR;
        }
    }

    applyPresetTransition (buffer);
}

bool CloudGreyVerbProcessor::hasEditor() const { return true; }

juce::AudioProcessorEditor* CloudGreyVerbProcessor::createEditor()
{
    return new CloudGreyVerbEditor (*this);
}

void CloudGreyVerbProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = parameters.copyState();
    // This is session metadata, deliberately not an automatable DSP parameter.
    state.setProperty ("factoryPresetIndex", currentPresetIndex, nullptr);
    if (auto xmlState = state.createXml())
        copyXmlToBinary (*xmlState, destData);
}

void CloudGreyVerbProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xmlState = getXmlFromBinary (data, sizeInBytes))
    {
        const auto restoredState = juce::ValueTree::fromXml (*xmlState);
        // ValueTree XML round-trips properties as strings, so accept both an
        // integral var and its textual form before validating the index.
        const auto storedIndex = restoredState.getProperty ("factoryPresetIndex");
        int numericIndex = -1;
        if (storedIndex.isInt() || storedIndex.isInt64())
            numericIndex = static_cast<int> (storedIndex);
        else if (storedIndex.isString())
            numericIndex = storedIndex.toString().getIntValue();
        // Pre-M5 sessions have no metadata; invalid data safely retains the
        // historical default base instead of exposing an arbitrary program.
        currentPresetIndex = numericIndex >= 0 && numericIndex < getNumPrograms() ? numericIndex : 0;
        presetTransactionGeneration.fetch_add (1, std::memory_order_acq_rel);
        parameters.replaceState (restoredState);
        const auto target = makeDspSnapshotFromParameters();
        if (!hasProcessedAudio.load (std::memory_order_acquire))
        {
            resetDspStateForTransition (target.hqMode);
            currentDspParams = resolveRuntimeParams (target);
            currentDspHqMode = target.hqMode;
            currentPreDelaySync = target.preDelaySync;
            currentSizeSync = target.sizeSync;
            currentSyncDivision = target.syncDivision;
            pendingPresetTargetPublished.store (false, std::memory_order_release);
            presetTransitionRequested.store (false, std::memory_order_release);
            presetTransitionStage.store (static_cast<int> (PresetTransitionStage::idle), std::memory_order_release);
        }
        else
        {
            publishRestoreTarget (target);
            requestPresetTransition();
        }
        presetTransactionGeneration.fetch_add (1, std::memory_order_release);
    }
}

// This creates new instances of the plugin
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new CloudGreyVerbProcessor();
}
