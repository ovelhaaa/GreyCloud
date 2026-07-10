#include "LookAndFeel.h"

GreyCloudLookAndFeel::GreyCloudLookAndFeel()
{
    backgroundColour = juce::Colour(24, 24, 28);
    accentColour = juce::Colour(245, 158, 11); // Amber/Orange
    outlineColour = juce::Colour(60, 60, 65);

    setColour(juce::Slider::textBoxTextColourId, juce::Colours::white);
    setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    
    setColour(juce::ComboBox::backgroundColourId, backgroundColour);
    setColour(juce::ComboBox::textColourId, juce::Colours::white);
    setColour(juce::ComboBox::outlineColourId, outlineColour);
    setColour(juce::ComboBox::arrowColourId, accentColour);

    setColour(juce::PopupMenu::backgroundColourId, backgroundColour.brighter(0.1f));
    setColour(juce::PopupMenu::textColourId, juce::Colours::white);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, accentColour);
    setColour(juce::PopupMenu::highlightedTextColourId, backgroundColour);
}

void GreyCloudLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                                            float sliderPos, const float rotaryStartAngle,
                                            const float rotaryEndAngle, juce::Slider& slider)
{
    auto radius = (float) juce::jmin(width / 2, height / 2) - 4.0f;
    auto centreX = (float) x + (float) width  * 0.5f;
    auto centreY = (float) y + (float) height * 0.5f;
    auto rx = centreX - radius;
    auto ry = centreY - radius;
    auto rw = radius * 2.0f;
    auto angle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);
    
    // Check if it's a macro slider (size > 50 roughly indicates macro, prompt says ~70px)
    bool isMacro = width > 50;
    float arcThickness = isMacro ? 4.0f : 2.0f;

    // Background track
    juce::Path backgroundArc;
    backgroundArc.addCentredArc(centreX, centreY, radius, radius, 0.0f, rotaryStartAngle, rotaryEndAngle, true);
    g.setColour(outlineColour);
    g.strokePath(backgroundArc, juce::PathStrokeType(arcThickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Value track
    if (slider.isEnabled())
    {
        juce::Path valueArc;
        valueArc.addCentredArc(centreX, centreY, radius, radius, 0.0f, rotaryStartAngle, angle, true);
        g.setColour(accentColour);
        g.strokePath(valueArc, juce::PathStrokeType(arcThickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // Thumb (indicator)
    juce::Path thumb;
    auto thumbWidth = isMacro ? 4.0f : 2.5f;
    thumb.addRectangle(-thumbWidth * 0.5f, -radius, thumbWidth, radius * 0.4f);
    g.setColour(slider.isEnabled() ? juce::Colours::white : juce::Colours::grey);
    g.fillPath(thumb, juce::AffineTransform::rotation(angle).translated(centreX, centreY));
}

void GreyCloudLookAndFeel::drawToggleButton(juce::Graphics& g, juce::ToggleButton& button,
                                            bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown)
{
    auto bounds = button.getLocalBounds().toFloat().reduced(1.0f);
    auto cornerSize = 4.0f;

    bool state = button.getToggleState();
    bool enabled = button.isEnabled();

    if (state)
    {
        g.setColour(enabled ? accentColour : accentColour.withAlpha(0.5f));
        g.fillRoundedRectangle(bounds, cornerSize);
    }
    else
    {
        g.setColour(enabled ? backgroundColour : backgroundColour.darker());
        g.fillRoundedRectangle(bounds, cornerSize);
        g.setColour(enabled ? outlineColour : outlineColour.withAlpha(0.5f));
        g.drawRoundedRectangle(bounds, cornerSize, 1.0f);
    }

    g.setColour(state ? backgroundColour : (enabled ? juce::Colours::white : juce::Colours::grey));
    g.setFont(13.0f);
    g.drawText(button.getButtonText(), button.getLocalBounds(), juce::Justification::centred, true);
}

void GreyCloudLookAndFeel::drawComboBox (juce::Graphics& g, int width, int height, bool isButtonDown,
                                         int buttonX, int buttonY, int buttonW, int buttonH, juce::ComboBox& box)
{
    auto cornerSize = 4.0f;
    juce::Rectangle<int> boxBounds(0, 0, width, height);
    
    g.setColour(box.isEnabled() ? backgroundColour : backgroundColour.darker());
    g.fillRoundedRectangle(boxBounds.toFloat(), cornerSize);
    
    g.setColour(box.isEnabled() ? outlineColour : outlineColour.withAlpha(0.5f));
    g.drawRoundedRectangle(boxBounds.toFloat().reduced(0.5f, 0.5f), cornerSize, 1.0f);
    
    juce::Rectangle<int> arrowZone(width - 20, 0, 20, height);
    juce::Path path;
    path.startNewSubPath(arrowZone.getX() + 5.0f, arrowZone.getCentreY() - 2.0f);
    path.lineTo(arrowZone.getCentreX(), arrowZone.getCentreY() + 3.0f);
    path.lineTo(arrowZone.getRight() - 5.0f, arrowZone.getCentreY() - 2.0f);
    
    g.setColour(box.isEnabled() ? accentColour : juce::Colours::grey);
    g.strokePath(path, juce::PathStrokeType(2.0f));
}

void GreyCloudLookAndFeel::drawLinearSlider(juce::Graphics& g, int x, int y, int width, int height,
                                             float sliderPos, float minSliderPos, float maxSliderPos,
                                             const juce::Slider::SliderStyle style, juce::Slider& slider)
{
    auto trackH = 5.0f;
    auto trackY = y + (height - trackH) * 0.5f;
    juce::Rectangle<float> track(x, trackY, width, trackH);

    g.setColour(outlineColour);
    g.fillRoundedRectangle(track, trackH * 0.5f);

    if (slider.isEnabled())
    {
        juce::Rectangle<float> fill(x, trackY, sliderPos - x, trackH);
        g.setColour(accentColour);
        g.fillRoundedRectangle(fill, trackH * 0.5f);
    }

    auto thumbW = 3.0f;
    g.setColour(slider.isEnabled() ? juce::Colours::white : juce::Colours::grey);
    g.fillRoundedRectangle(sliderPos - thumbW * 0.5f, y + 1.0f, thumbW, height - 2.0f, 1.5f);
}
