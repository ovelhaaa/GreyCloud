#include "PluginProcessor.h"
#include "PluginEditor.h"

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

    return { params.begin(), params.end() };
}

CloudGreyVerbProcessor::CloudGreyVerbProcessor()
    : AudioProcessor (BusesProperties()
                      .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                      .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      parameters (*this, nullptr, juce::Identifier ("CloudGreyVerbVTS"), createParameterLayout())
{
}

CloudGreyVerbProcessor::~CloudGreyVerbProcessor() = default;

const juce::String CloudGreyVerbProcessor::getName() const { return JucePlugin_Name; }
bool CloudGreyVerbProcessor::acceptsMidi() const { return false; }
bool CloudGreyVerbProcessor::producesMidi() const { return false; }
bool CloudGreyVerbProcessor::isMidiEffect() const { return false; }
double CloudGreyVerbProcessor::getTailLengthSeconds() const { return 0.0; }
int CloudGreyVerbProcessor::getNumPrograms() { return 1; }
int CloudGreyVerbProcessor::getCurrentProgram() { return 0; }
void CloudGreyVerbProcessor::setCurrentProgram (int index) {}
const juce::String CloudGreyVerbProcessor::getProgramName (int index) { return {}; }
void CloudGreyVerbProcessor::changeProgramName (int index, const juce::String& newName) {}

void CloudGreyVerbProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    // Allocate DSP memory (~1.5MB for stereo 48k operations)
    size_t requiredFloats = 800000; 
    dspMemory.resize(requiredFloats, 0.0f);
    
    dspCore.init(static_cast<float>(sampleRate), dspMemory.data(), requiredFloats);
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
    cgv::Params p;
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
    p.inputGain = parameters.getRawParameterValue("inputGain")->load();
    p.outputGain = parameters.getRawParameterValue("outputGain")->load();
    p.preDelay = parameters.getRawParameterValue("preDelay")->load();
    p.stereoWidth = parameters.getRawParameterValue("stereoWidth")->load();

    dspCore.setParams(p);

    int numSamples = buffer.getNumSamples();
    float* channelL = buffer.getWritePointer(0);
    float* channelR = (totalNumOutputChannels > 1) ? buffer.getWritePointer(1) : nullptr;

    for (int i = 0; i < numSamples; ++i) {
        float inL = channelL[i];
        float inR = channelR ? channelR[i] : inL;
        
        float outL = 0.0f;
        float outR = 0.0f;
        
        dspCore.process(inL, inR, outL, outR);
        
        channelL[i] = outL;
        if (channelR) channelR[i] = outR;
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
