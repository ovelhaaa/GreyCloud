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

    struct BuiltInPreset {
        juce::String name;
        float mix, texture, freeze, feedback, size, diffusion, modDepth, modRate, damping, lowDamping, tone, inputGain, outputGain, shimmer, preDelay, stereoWidth;
        int shimmerRatioIndex;
        bool hqMode;
        float reverseMix, grainScan;
        bool stereoCoreOn;
        bool hardFreezeOn;
        bool preDelaySyncOn;
        bool sizeSyncOn;
        int syncDivisionIndex;
        
        BuiltInPreset(juce::String n, float m, float t, float fr, float fb, float s, float d, float md, float mr, float da, float lda, float to, float ig, float og, float sh, float pd, float sw, int sri = 2, bool hq = false, float revMix = 0.0f, float gScan = 0.0f, bool stereoCore = true, bool hardFreeze = false, bool preDelaySync = false, bool sizeSync = false, int syncDiv = 7)
            : name(n), mix(m), texture(t), freeze(fr), feedback(fb), size(s), diffusion(d), modDepth(md), modRate(mr), damping(da), lowDamping(lda), tone(to), inputGain(ig), outputGain(og), shimmer(sh), preDelay(pd), stereoWidth(sw), shimmerRatioIndex(sri), hqMode(hq), reverseMix(revMix), grainScan(gScan), stereoCoreOn(stereoCore), hardFreezeOn(hardFreeze), preDelaySyncOn(preDelaySync), sizeSyncOn(sizeSync), syncDivisionIndex(syncDiv) {}
    };

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

private:
    enum class PresetTransitionStage
    {
        idle = 0,
        fadeOut,
        fadeIn
    };

    void applyPresetTransition (juce::AudioBuffer<float>& buffer);
    void resetDspStateForPresetChange();
    int getPresetTransitionLengthInSamples (double seconds) const;

    juce::AudioProcessorValueTreeState parameters;
    
    CloudGreyVerb dspCoreNormal;
    CloudGreyVerb dspCoreHQ;
    std::vector<float> dspMemoryNormal;
    std::vector<float> dspMemoryHQ;
    
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampling;
    juce::dsp::DelayLine<float> latencyCompensationL { 1024 };
    juce::dsp::DelayLine<float> latencyCompensationR { 1024 };
    float currentLatencySamples = 0.0f;
    
    std::vector<BuiltInPreset> presets;
    int currentPresetIndex = 0;
    double currentSampleRate = 44100.0;
    CloudGreyVerb::Params currentDspParams;
    bool currentDspHqMode = false;
    std::atomic<bool> presetTransitionRequested { false };
    std::atomic<int> presetTransitionStage { static_cast<int> (PresetTransitionStage::idle) };
    int presetTransitionSamplesRemaining = 0;
    int presetTransitionSamplesTotal = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CloudGreyVerbProcessor)
};
