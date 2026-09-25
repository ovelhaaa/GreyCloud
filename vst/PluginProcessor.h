#pragma once

#include <JuceHeader.h>
#include "cloud_grey_verb.hpp"
#include <atomic>
#include <vector>
#include <memory>
#include <juce_dsp/juce_dsp.h>

class CloudGreyVerbProcessor : public juce::AudioProcessor
{
public:
    CloudGreyVerbProcessor();
    ~CloudGreyVerbProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    const juce::String getName() const override;
    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& getVTS() { return parameters; }
    void requestPresetTransition();
    float getLastRuntimePreDelaySecondsForTest() const { return lastRuntimePreDelaySeconds.load(); }
    bool areCoresReadyForTest() const { return coresReady; }

private:
    enum class PresetTransitionStage
    {
        idle = 0,
        fadeOut,
        fadeIn
    };

    void applyPresetTransition (juce::AudioBuffer<float>& buffer);
    void resetDspStateForTransition (bool targetHq);
    void publishPresetTarget (const CloudGreyVerb::FactoryPreset& preset);
    int getPresetTransitionLengthInSamples (double seconds) const;

    juce::AudioProcessorValueTreeState parameters;
    
    CloudGreyVerb dspCoreNormal;
    CloudGreyVerb dspCoreHQ;
    std::vector<float> dspMemoryNormal;
    std::vector<float> dspMemoryHQ;
    
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampling;
    juce::dsp::DelayLine<float> latencyCompensationL { 1024 };
    juce::dsp::DelayLine<float> latencyCompensationR { 1024 };
    // Separate delayed dry reference used only to fade wet state during preset
    // resets; it prevents the transition envelope from punching a hole in dry.
    juce::dsp::DelayLine<float> transitionDryDelayL { 1024 };
    juce::dsp::DelayLine<float> transitionDryDelayR { 1024 };
    juce::AudioBuffer<float> transitionDryBuffer;
    float currentLatencySamples = 0.0f;
    
    int currentPresetIndex = 0;
    double currentSampleRate = 44100.0;
    CloudGreyVerb::Params currentDspParams;
    bool currentDspHqMode = false;
    bool coresReady = false;
    struct TransitionTarget { CloudGreyVerb::Params params; bool hqMode = false; };
    // Two published slots avoid exposing a partially-written factory program
    // to the callback. The atomic index is the publication fence.
    TransitionTarget pendingPresetTargets[2];
    std::atomic<int> pendingPresetTargetIndex { 0 };
    std::atomic<bool> pendingPresetTargetPublished { false };
    std::atomic<unsigned> presetTransactionGeneration { 0 };
    std::atomic<bool> presetTransitionRequested { false };
    TransitionTarget transitionTarget;
    std::atomic<int> presetTransitionStage { static_cast<int> (PresetTransitionStage::idle) };
    int presetTransitionSamplesRemaining = 0;
    int presetTransitionSamplesTotal = 0;
    std::atomic<float> lastRuntimePreDelaySeconds { -1.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CloudGreyVerbProcessor)
};
