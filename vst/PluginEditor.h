#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "LookAndFeel.h"
#include <vector>
#include <memory>

class CardComponent : public juce::Component
{
public:
    CardComponent(const juce::String& title) : name(title) {}
    void paint(juce::Graphics& g) override {
        auto bounds = getLocalBounds().toFloat();
        auto titleArea = bounds.withHeight(20.0f).reduced(1.0f, 1.0f);

        g.setColour(juce::Colour(60, 60, 65));
        g.drawRoundedRectangle(bounds, 4.0f, 1.0f);
        g.setColour(juce::Colour(34, 34, 39));
        g.fillRoundedRectangle(titleArea, 3.0f);
        g.setColour(juce::Colour(221, 191, 114));
        g.drawHorizontalLine(20, bounds.getX() + 7.0f, bounds.getRight() - 7.0f);
        g.setFont(juce::Font(11.0f, juce::Font::bold));
        g.drawText(name.toUpperCase(), titleArea.toNearestInt(), juce::Justification::centred, false);
    }
private:
    juce::String name;
};

class SubgroupComponent : public juce::Component
{
public:
    SubgroupComponent(const juce::String& title) : name(title) {}
    void paint(juce::Graphics& g) override {
        auto bounds = getLocalBounds().toFloat();
        auto titleArea = bounds.withHeight(18.0f).reduced(1.0f, 1.0f);

        g.setColour(juce::Colour(221, 191, 114));
        g.drawRoundedRectangle(bounds, 4.0f, 1.0f);
        g.setColour(juce::Colour(221, 191, 114).withAlpha(0.16f));
        g.fillRoundedRectangle(titleArea, 3.0f);
        g.setColour(juce::Colour(221, 191, 114));
        g.setFont(juce::Font(10.5f, juce::Font::bold));
        g.drawText(name.toUpperCase(), titleArea.toNearestInt(), juce::Justification::centred, false);
    }
private:
    juce::String name;
};

class CloudGreyVerbEditor  : public juce::AudioProcessorEditor, private juce::AudioProcessorParameter::Listener
{
public:
    CloudGreyVerbEditor (CloudGreyVerbProcessor&);
    ~CloudGreyVerbEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    
    void parameterValueChanged (int parameterIndex, float newValue) override;
    void parameterGestureChanged (int parameterIndex, bool gestureIsStarting) override {}

private:
    CloudGreyVerbProcessor& audioProcessor;
    GreyCloudLookAndFeel customLookAndFeel;

    struct RotaryControl {
        juce::Slider slider;
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };
    
    struct ToggleControl {
        juce::ToggleButton button;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> attachment;
    };
    
    struct ChoiceControl {
        juce::ComboBox comboBox;
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> attachment;
    };

    std::vector<std::unique_ptr<RotaryControl>> rotaryControls;
    std::vector<std::unique_ptr<ToggleControl>> toggleControls;
    std::vector<std::unique_ptr<ChoiceControl>> choiceControls;
    std::vector<std::unique_ptr<CardComponent>> cards;
    std::unique_ptr<SubgroupComponent> freezeSubgroup;
    std::unique_ptr<juce::Component> nimbusLogo;

    juce::ComboBox presetSelector;

    void addRotaryControl(const juce::String& paramID, const juce::String& name);
    void addFaderControl(const juce::String& paramID, const juce::String& name);
    void addToggleControl(const juce::String& paramID, const juce::String& name);
    void addChoiceControl(const juce::String& paramID, const juce::String& name, bool hideLabel = false);
    
    RotaryControl* getRotary(const juce::String& paramID);
    ToggleControl* getToggle(const juce::String& paramID);
    ChoiceControl* getChoice(const juce::String& paramID);

    void loadJSONPreset();
    void exportJSONPreset();
    void updateSyncState();

    juce::TextButton importButton;
    juce::TextButton exportButton;
    std::unique_ptr<juce::FileChooser> fileChooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CloudGreyVerbEditor)
};
