#pragma once

#include <JuceHeader.h>

// Shared semantic colours and type sizes for the native editor.
namespace NimbusStyle
{
inline const juce::Colour background { 0xff18181c }, raised { 0xff1e1e23 }, deep { 0xff111111 };
inline const juce::Colour border { 0xff2a2a30 }, strongBorder { 0xff45454d };
inline const juce::Colour text { 0xfff4f2ea }, secondary { 0xffaaa7a0 };
inline const juce::Colour gold { 0xffddbf72 }, mutedGold { 0xff93825a };
constexpr float caption = 8.0f, label = 10.5f, status = 10.0f;
juce::Font controlFont(float height);
juce::Font regularFont(float height);
juce::Font captionFont(float height);

}

class GreyCloudLookAndFeel : public juce::LookAndFeel_V4
{
public:
    GreyCloudLookAndFeel();

    void drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                          float sliderPos, const float rotaryStartAngle,
                          const float rotaryEndAngle, juce::Slider& slider) override;

    void drawToggleButton(juce::Graphics& g, juce::ToggleButton& button,
                          bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;

    void drawLinearSlider(juce::Graphics& g, int x, int y, int width, int height,
                          float sliderPos, float minSliderPos, float maxSliderPos,
                          const juce::Slider::SliderStyle style, juce::Slider& slider) override;

    void drawComboBox (juce::Graphics& g, int width, int height, bool isButtonDown,
                       int buttonX, int buttonY, int buttonW, int buttonH, juce::ComboBox& box) override;

    juce::Font getComboBoxFont(juce::ComboBox& box);
    void positionComboBoxText(juce::ComboBox& box, juce::Label& label);
    juce::Font getPopupMenuFont();
    juce::Font getSliderPopupFont(juce::Slider& slider);

    void drawButtonBackground(juce::Graphics& g, juce::Button& button,
                              const juce::Colour& backgroundColour,
                              bool shouldDrawButtonAsHighlighted,
                              bool shouldDrawButtonAsDown) override;

    void drawButtonText(juce::Graphics& g, juce::TextButton& button,
                        bool shouldDrawButtonAsHighlighted,
                        bool shouldDrawButtonAsDown) override;

private:
    juce::Colour backgroundColour;
    juce::Colour accentColour;
    juce::Colour outlineColour;
};
