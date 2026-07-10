#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "TempoSyncUtils.h"

// Factory function to create parameters
juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    params.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{"mix", 1}, "Mix", 0.0f, 1.0f, 0.5f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{"texture", 1}, "Texture", 0.0f, 1.0f, 0.5f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{"freeze", 1}, "Freeze", 0.0f, 1.0f, 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{"feedback", 1}, "Feedback", 0.0f, 0.94f, 0.5f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{"size", 1}, "Size", 0.0f, 1.0f, 0.5f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{"diffusion", 1}, "Diffusion", 0.0f, 1.0f, 0.5f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{"modDepth", 1}, "Mod Depth", 0.0f, 1.0f, 0.2f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{"modRate", 1}, "Mod Rate", 0.0f, 1.0f, 0.2f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{"damping", 1}, "Damping", 0.0f, 1.0f, 0.5f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{"lowDamping", 1}, "Low Damp", 0.0f, 1.0f, 0.5f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{"tone", 1}, "Tone", 0.0f, 1.0f, 0.5f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{"shimmer", 1}, "Shimmer", 0.0f, 1.0f, 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{"inputGain", 1}, "Input Gain", 0.0f, 2.0f, 1.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{"outputGain", 1}, "Output Gain", 0.0f, 2.0f, 1.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{"preDelay", 1}, "Pre-Delay", 0.0f, 1.0f, 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{"stereoWidth", 1}, "Stereo Width", 0.0f, 2.0f, 1.0f));

    juce::StringArray shimmerChoices = { "-1 Oct", "+5th", "+1 Oct", "+1 Oct & 5th", "+2 Oct" };
    params.push_back(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{"shimmerRatio", 1}, "Shimmer Ratio", shimmerChoices, 2));
    
    params.push_back(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"stereoCore", 1}, "Stereo Core", true));
    params.push_back(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"hardFreeze", 1}, "Hard Freeze", false));
    params.push_back(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"hqMode", 1}, "HQ Mode", false));

    params.push_back(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"preDelaySync", 1}, "Pre-Delay Sync", false));
    params.push_back(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"sizeSync", 1}, "Size Sync", false));
    
    juce::StringArray syncChoices = { "1/32", "1/16", "1/16T", "1/16D", "1/8", "1/8T", "1/8D", "1/4", "1/4T", "1/4D", "1/2", "1/1", "2/1" };
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
    presets.push_back(BuiltInPreset("SmallCloudRoom", 0.4f, 0.3f, 0.0f, 0.5f, 0.35f, 0.6f, 0.2f, 0.15f, 0.5f, 0.5f, 0.6f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f));
    presets.push_back(BuiltInPreset("BassAmbientWash", 0.36f, 0.42f, 0.0f, 0.62f, 0.56f, 0.52f, 0.14f, 0.15f, 0.78f, 0.2f, 0.40f, 0.90f, 0.92f, 0.0f, 0.1f, 1.5f));
    presets.push_back(BuiltInPreset("FrozenOrganPad", 0.7f, 0.85f, 1.0f, 0.65f, 0.7f, 0.8f, 0.4f, 0.05f, 0.4f, 0.6f, 0.45f, 1.0f, 1.0f, 0.0f, 0.0f, 1.2f));
    presets.push_back(BuiltInPreset("GreyholeDelayVerb", 0.6f, 0.55f, 0.0f, 0.76f, 0.76f, 0.70f, 0.4f, 0.25f, 0.65f, 0.5f, 0.5f, 1.0f, 0.90f, 0.0f, 0.2f, 1.0f));
    presets.push_back(BuiltInPreset("DarkLongCloud", 0.55f, 0.75f, 0.0f, 0.76f, 0.84f, 0.66f, 0.3f, 0.1f, 0.3f, 0.4f, 0.3f, 0.72f, 0.72f, 0.0f, 0.3f, 1.0f));
    presets.push_back(BuiltInPreset("GlitchSmear", 0.5f, 0.05f, 0.0f, 0.5f, 0.25f, 0.2f, 0.9f, 0.8f, 0.5f, 0.5f, 0.5f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f));
    presets.push_back(BuiltInPreset("AlwaysOnSubtle", 0.25f, 0.2f, 0.0f, 0.3f, 0.2f, 0.4f, 0.1f, 0.1f, 0.5f, 0.5f, 0.5f, 1.0f, 1.0f, 0.0f, 0.05f, 0.8f));
    presets.push_back(BuiltInPreset("BrightCloud", 0.5f, 0.6f, 0.0f, 0.75f, 0.6f, 0.7f, 0.6f, 0.4f, 0.7f, 0.8f, 0.8f, 1.0f, 1.0f, 0.0f, 0.1f, 1.2f));
    presets.push_back(BuiltInPreset("ShimmerCloud", 0.55f, 0.55f, 0.0f, 0.58f, 0.62f, 0.70f, 0.20f, 0.12f, 0.55f, 0.6f, 0.62f, 0.80f, 0.85f, 0.20f, 0.15f, 1.4f, 2, true));
    presets.push_back(BuiltInPreset("ReverseSmear", 0.65f, 0.6f, 0.0f, 0.70f, 0.5f, 0.6f, 0.4f, 0.2f, 0.6f, 0.5f, 0.5f, 1.0f, 1.0f, 0.0f, 0.0f, 1.2f, 2, false, 1.0f, 1.0f));
    currentPresetIndex = 0;
}

CloudGreyVerbProcessor::~CloudGreyVerbProcessor() = default;

const juce::String CloudGreyVerbProcessor::getName() const { return JucePlugin_Name; }
bool CloudGreyVerbProcessor::acceptsMidi() const { return false; }
bool CloudGreyVerbProcessor::producesMidi() const { return false; }
bool CloudGreyVerbProcessor::isMidiEffect() const { return false; }
double CloudGreyVerbProcessor::getTailLengthSeconds() const { return 0.0; }
int CloudGreyVerbProcessor::getNumPrograms() { return static_cast<int>(presets.size()); }
int CloudGreyVerbProcessor::getCurrentProgram() { return currentPresetIndex; }

void CloudGreyVerbProcessor::setCurrentProgram (int index)
{
    if (index >= 0 && index < presets.size())
    {
        currentPresetIndex = index;
        const auto& p = presets[index];
        
        auto updateParameterValue = [&](const juce::String& id, float value) {
            if (auto* param = parameters.getParameter(id))
                param->setValueNotifyingHost(param->convertTo0to1(value));
        };
        
        updateParameterValue("mix", p.mix);
        updateParameterValue("texture", p.texture);
        updateParameterValue("freeze", p.freeze);
        updateParameterValue("feedback", p.feedback);
        updateParameterValue("size", p.size);
        updateParameterValue("diffusion", p.diffusion);
        updateParameterValue("modDepth", p.modDepth);
        updateParameterValue("modRate", p.modRate);
        updateParameterValue("damping", p.damping);
        updateParameterValue("lowDamping", p.lowDamping);
        updateParameterValue("tone", p.tone);
        updateParameterValue("inputGain", p.inputGain);
        updateParameterValue("outputGain", p.outputGain);
        updateParameterValue("shimmer", p.shimmer);
        updateParameterValue("shimmerRatio", static_cast<float>(p.shimmerRatioIndex));
        updateParameterValue("preDelay", p.preDelay);
        updateParameterValue("stereoWidth", p.stereoWidth);
        updateParameterValue("hqMode", p.hqMode ? 1.0f : 0.0f);
        updateParameterValue("reverseMix", p.reverseMix);
        updateParameterValue("grainScan", p.grainScan);
        updateParameterValue("stereoCore", p.stereoCoreOn ? 1.0f : 0.0f);
        updateParameterValue("hardFreeze", p.hardFreezeOn ? 1.0f : 0.0f);
        updateParameterValue("preDelaySync", p.preDelaySyncOn ? 1.0f : 0.0f);
        updateParameterValue("sizeSync", p.sizeSyncOn ? 1.0f : 0.0f);
        updateParameterValue("syncDivision", static_cast<float>(p.syncDivisionIndex));
    }
}

const juce::String CloudGreyVerbProcessor::getProgramName (int index) 
{ 
    if (index >= 0 && index < presets.size())
        return presets[index].name;
    return {}; 
}

void CloudGreyVerbProcessor::changeProgramName (int index, const juce::String& newName) 
{
    if (index >= 0 && index < presets.size())
        presets[index].name = newName;
}

void CloudGreyVerbProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    // Allocate DSP memory (~3.0MB for true stereo 48k operations)
    size_t requiredFloats = 1600000; 
    dspMemoryNormal.resize(requiredFloats, 0.0f);
    
    // For HQ mode (2x oversampling), we need to handle 2x sample rate without halving max delay times.
    size_t requiredFloatsHQ = requiredFloats * 2;
    dspMemoryHQ.resize(requiredFloatsHQ, 0.0f);
    
    dspCoreNormal.init(static_cast<float>(sampleRate), dspMemoryNormal.data(), requiredFloats);
    dspCoreHQ.init(static_cast<float>(sampleRate * 2.0), dspMemoryHQ.data(), requiredFloatsHQ);
    
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
    latencyCompensationL.setDelay(currentLatencySamples);
    latencyCompensationR.setDelay(currentLatencySamples);
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

    for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear (i, 0, buffer.getNumSamples());

    // Update DSP parameters from VTS
    CloudGreyVerb::Params p;
    p.mix = parameters.getRawParameterValue("mix")->load();
    p.texture = parameters.getRawParameterValue("texture")->load();
    p.freeze = parameters.getRawParameterValue("freeze")->load();
    p.feedback = parameters.getRawParameterValue("feedback")->load();
    p.size = parameters.getRawParameterValue("size")->load();
    p.diffusion = parameters.getRawParameterValue("diffusion")->load();
    p.modDepth = parameters.getRawParameterValue("modDepth")->load();
    p.modRate = parameters.getRawParameterValue("modRate")->load();
    p.damping = parameters.getRawParameterValue("damping")->load();
    p.lowDamping = parameters.getRawParameterValue("lowDamping")->load();
    p.tone = parameters.getRawParameterValue("tone")->load();
    p.shimmer = parameters.getRawParameterValue("shimmer")->load();
    p.shimmerRatioIndex = static_cast<int>(parameters.getRawParameterValue("shimmerRatio")->load());
    p.inputGain = parameters.getRawParameterValue("inputGain")->load();
    p.outputGain = parameters.getRawParameterValue("outputGain")->load();
    p.preDelay = parameters.getRawParameterValue("preDelay")->load();
    p.stereoWidth = parameters.getRawParameterValue("stereoWidth")->load();
    p.stereoCore = parameters.getRawParameterValue("stereoCore")->load() > 0.5f;
    p.hardFreeze = parameters.getRawParameterValue("hardFreeze")->load() > 0.5f;
    p.reverseMix = parameters.getRawParameterValue("reverseMix")->load();
    p.grainScan = parameters.getRawParameterValue("grainScan")->load();
    
    bool hqMode = parameters.getRawParameterValue("hqMode")->load() > 0.5f;
    bool preDelaySync = parameters.getRawParameterValue("preDelaySync")->load() > 0.5f;
    bool sizeSync = parameters.getRawParameterValue("sizeSync")->load() > 0.5f;
    int syncDivision = static_cast<int>(parameters.getRawParameterValue("syncDivision")->load());

    if (preDelaySync || sizeSync) {
        float bpm = 120.0f;
        if (auto* playHead = getPlayHead()) {
            if (auto pos = playHead->getPosition()) {
                if (pos->getBpm().hasValue()) {
                    bpm = static_cast<float>(*pos->getBpm());
                }
            }
        }
        
        float syncMs = TempoSyncUtils::getMsFromBpm(bpm, syncDivision);
        
        if (preDelaySync) {
            // max predelay is 200ms based on engine logic
            // normalized preDelay: ms / 200.0f
            // silently clamped as 200ms is the physical buffer limit of the engine
            float normalized = syncMs / 200.0f;
            p.preDelay = juce::jlimit(0.0f, 1.0f, normalized);
        }
        
        if (sizeSync) {
            float sr = static_cast<float>(getSampleRate());
            if (hqMode) sr *= 2.0f;
            
            float targetFrames = syncMs * (sr / 1000.0f);
            size_t mainDelaySize = hqMode ? dspCoreHQ.getMainDelayFrames() : dspCoreNormal.getMainDelayFrames();
            
            float minFrames = sr * CloudGreyVerb::kSizeMinFrameRatio;
            float maxFrames = static_cast<float>(mainDelaySize) * CloudGreyVerb::kSizeMaxFrameRatio;
            
            float normalizedSize = (targetFrames - minFrames) / (maxFrames - minFrames);
            p.size = juce::jlimit(0.0f, 1.0f, normalizedSize);
        }
    }

    if (hqMode) {
        dspCoreHQ.setParams(p);
    } else {
        dspCoreNormal.setParams(p);
    }

    if (hqMode) {
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
}

bool CloudGreyVerbProcessor::hasEditor() const { return true; }

juce::AudioProcessorEditor* CloudGreyVerbProcessor::createEditor()
{
    return new CloudGreyVerbEditor (*this);
}

void CloudGreyVerbProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xmlState = parameters.copyState().createXml())
        copyXmlToBinary (*xmlState, destData);
}

void CloudGreyVerbProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xmlState = getXmlFromBinary (data, sizeInBytes))
        parameters.replaceState (juce::ValueTree::fromXml (*xmlState));
}

// This creates new instances of the plugin
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new CloudGreyVerbProcessor();
}
