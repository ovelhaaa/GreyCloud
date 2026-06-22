#pragma once

#include <JuceHeader.h>
#include "cloud_grey_verb.hpp"
#include <vector>
#include <memory>

class CloudGreyVerbProcessor : public juce::AudioProcessor
{
public:
    CloudGreyVerbProcessor();
    ~CloudGreyVerbProcessor() override;

    struct BuiltInPreset {
        juce::String name;
        float mix, texture, freeze, feedback, size, diffusion, modDepth, modRate, damping, lowDamping, tone, inputGain, outputGain, shimmer, preDelay, stereoWidth;
        
        BuiltInPreset(juce::String n, float m, float t, float fr, float fb, float s, float d, float md, float mr, float da, float lda, float to, float ig, float og, float sh, float pd, float sw)
            : name(n), mix(m), texture(t), freeze(fr), feedback(fb), size(s), diffusion(d), modDepth(md), modRate(mr), damping(da), lowDamping(lda), tone(to), inputGain(ig), outputGain(og), shimmer(sh), preDelay(pd), stereoWidth(sw) {}
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

private:
    juce::AudioProcessorValueTreeState parameters;
    CloudGreyVerb dspCore;
    std::vector<float> dspMemory;
    
    std::vector<BuiltInPreset> presets;
    int currentPresetIndex = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CloudGreyVerbProcessor)
};
