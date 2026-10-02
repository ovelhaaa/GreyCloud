#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "LookAndFeel.h"
#include <vector>
#include <memory>

class CardComponent : public juce::Component
{
public:
    CardComponent(const juce::String& title, float width) : name(title), referenceWidth(width) {}
    void paint(juce::Graphics& g) override {
        auto bounds = getLocalBounds().toFloat();
        g.setColour(NimbusStyle::raised);
        g.fillRoundedRectangle(bounds, 5.0f);
        g.setColour(NimbusStyle::border);
        g.drawRoundedRectangle(bounds.reduced(0.5f), 5.0f, 1.0f);
        g.setColour(NimbusStyle::secondary);
        g.setFont(NimbusStyle::captionFont(NimbusStyle::caption * getWidth() / referenceWidth));
        g.drawText(name.toUpperCase(), bounds.withHeight(18.0f * getWidth() / referenceWidth).toNearestInt(),
                   juce::Justification::centred, false);
    }
private:
    juce::String name;
    float referenceWidth;
};

class SubgroupComponent : public juce::Component
{
public:
    SubgroupComponent() = default;
    void setActivity(bool freeze, bool hard) {
        const int next = hard ? 2 : freeze ? 1 : 0;
        if (next != activity) { activity = next; repaint(); }
    }
    void paint(juce::Graphics& g) override {
        const auto bounds = getLocalBounds().toFloat().reduced(0.5f);
        g.setColour(activity ? NimbusStyle::gold.withAlpha(0.04f) : NimbusStyle::deep.withAlpha(0.15f));
        g.fillRoundedRectangle(bounds, 4.0f);
        g.setColour(activity ? NimbusStyle::gold.withAlpha(activity == 2 ? 0.65f : 0.35f) : NimbusStyle::border);
        g.drawRoundedRectangle(bounds, 4.0f, 1.0f);

    }

private:
    int activity = 0;
};

class CloudGreyVerbEditor  : public juce::AudioProcessorEditor, private juce::AudioProcessorParameter::Listener, private juce::Timer
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

    juce::GlyphArrangement brandTitle, brandDescriptor;
    juce::ComboBox presetSelector;
    juce::Label presetStatus;
    juce::Label syncFeedback;
    juce::TextButton previousPreset { juce::String::fromUTF8 ("\xE2\x97\x80") }, nextPreset { juce::String::fromUTF8 ("\xE2\x96\xB6") };
    std::unique_ptr<juce::TooltipWindow> tooltipWindow;

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
    void timerCallback() override;

    juce::TextButton importButton;
    juce::TextButton exportButton;
    std::unique_ptr<juce::FileChooser> fileChooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CloudGreyVerbEditor)
};
