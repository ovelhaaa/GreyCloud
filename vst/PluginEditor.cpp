#include "PluginProcessor.h"
#include "PluginEditor.h"

CloudGreyVerbEditor::CloudGreyVerbEditor (CloudGreyVerbProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p)
{
    addDSPControl("mix", "Mix");
    addDSPControl("texture", "Texture");
    addDSPControl("freeze", "Freeze");
    addDSPControl("feedback", "Feedback");
    addDSPControl("size", "Size");
    addDSPControl("diffusion", "Diffusion");
    addDSPControl("modDepth", "Mod Depth");
    addDSPControl("modRate", "Mod Rate");
    addDSPControl("damping", "Damping");
    addDSPControl("lowDamping", "Low Damp");
    addDSPControl("tone", "Tone");
    addDSPControl("shimmer", "Shimmer");
    addDSPControl("preDelay", "Pre-Delay");
    addDSPControl("stereoWidth", "Stereo Width");
    addDSPControl("inputGain", "Input");
    addDSPControl("outputGain", "Output");

    // Dimensions: 4 columns x 4 rows
    setSize (600, 450);
}

CloudGreyVerbEditor::~CloudGreyVerbEditor()
{
}

void CloudGreyVerbEditor::addDSPControl(const juce::String& paramID, const juce::String& name) {
    auto wrapper = std::make_unique<PSlider>();
    
    wrapper->slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    wrapper->slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 50, 15);
    addAndMakeVisible(wrapper->slider);

    wrapper->label.setText(name, juce::dontSendNotification);
    wrapper->label.setJustificationType(juce::Justification::centred);
    wrapper->label.attachToComponent(&wrapper->slider, false);
    addAndMakeVisible(wrapper->label);

    wrapper->attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getVTS(), paramID, wrapper->slider);

    sliders.push_back(std::move(wrapper));
}

void CloudGreyVerbEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour(24, 24, 28));

    g.setColour (juce::Colours::white);
    g.setFont (24.0f);
    g.drawFittedText ("Nimbus Reverb", getLocalBounds().removeFromTop(40), juce::Justification::centred, 1);
}

void CloudGreyVerbEditor::resized()
{
    auto bounds = getLocalBounds().reduced(20);
    bounds.removeFromTop(30); // header spacing
    
    int numCols = 4;
    int numRows = 4;
    
    int w = bounds.getWidth() / numCols;
    int h = bounds.getHeight() / numRows;

    for (int i = 0; i < (int)sliders.size(); ++i) {
        int r = i / numCols;
        int c = i % numCols;
        
        auto cell = bounds.withTrimmedLeft(c * w).withTrimmedTop(r * h).withWidth(w).withHeight(h);
        // Leave room for label that was attached at the top natively by juce
        sliders[i]->slider.setBounds(cell.reduced(10).withTrimmedTop(15));
    }
}
