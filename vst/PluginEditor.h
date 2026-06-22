#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

class CloudGreyVerbEditor  : public juce::AudioProcessorEditor
{
public:
    CloudGreyVerbEditor (CloudGreyVerbProcessor&);
    ~CloudGreyVerbEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    CloudGreyVerbProcessor& audioProcessor;

    struct PSlider {
        juce::Slider slider;
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    std::vector<std::unique_ptr<PSlider>> sliders;

    void addDSPControl(const juce::String& paramID, const juce::String& name);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CloudGreyVerbEditor)
};
