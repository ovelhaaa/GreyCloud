#include "LookAndFeel.h"
#include "BinaryData.h"

namespace NimbusStyle
{
juce::Font controlFont(float height)
{
    static const auto face = juce::Typeface::createSystemTypefaceFor(BinaryData::MontserratLight_ttf,
                                                                   BinaryData::MontserratLight_ttfSize);
    return juce::Font(face).withHeight(height);
}

juce::Font regularFont(float height)
{
    static const auto face = juce::Typeface::createSystemTypefaceFor(BinaryData::MontserratRegular_ttf,
                                                                   BinaryData::MontserratRegular_ttfSize);
    return juce::Font(face).withHeight(height);
}

juce::Font captionFont(float height)
{
    static const auto face = juce::Typeface::createSystemTypefaceFor(BinaryData::MontserratBold_ttf,
                                                                   BinaryData::MontserratBold_ttfSize);
    return juce::Font(face).withHeight(height);
}
}


GreyCloudLookAndFeel::GreyCloudLookAndFeel()
{
    backgroundColour = NimbusStyle::background;
    accentColour = NimbusStyle::gold; // Nimbus logo gold
    outlineColour = NimbusStyle::strongBorder;

    setColour(juce::Slider::textBoxTextColourId, NimbusStyle::text);
    setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    
    setColour(juce::ComboBox::backgroundColourId, backgroundColour);
    setColour(juce::ComboBox::textColourId, NimbusStyle::text);
    setColour(juce::ComboBox::outlineColourId, outlineColour);
    setColour(juce::ComboBox::arrowColourId, accentColour);

    setColour(juce::PopupMenu::backgroundColourId, backgroundColour.brighter(0.1f));
    setColour(juce::PopupMenu::textColourId, NimbusStyle::text);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, accentColour);
    setColour(juce::PopupMenu::highlightedTextColourId, backgroundColour);
}

void GreyCloudLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                                            float sliderPos, const float rotaryStartAngle,
                                            const float rotaryEndAngle, juce::Slider& slider)
{
    // Parameter identity keeps the macro hierarchy consistent at every editor size.
    const auto id = slider.getName();
    const bool isMacro = id == "mix" || id == "size" || id == "feedback" || id == "texture";
    const float scale = (float) juce::jmin(width, height) / (isMacro ? 70.0f : id == "preDelay" || id == "stereoWidth" ? 34.0f : 36.0f);
    const auto radius = (float) juce::jmin(width, height) * 0.5f - 4.0f * scale;
    const auto centreX = (float) x + (float) width * 0.5f;
    const auto centreY = (float) y + (float) height * 0.5f;
    const auto angle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);
    const auto arcThickness = (isMacro ? 4.0f : 2.0f) * scale;

    const auto bodyRadius = radius * 0.77f;
    const juce::Rectangle<float> body(centreX - bodyRadius, centreY - bodyRadius,
                                     bodyRadius * 2.0f, bodyRadius * 2.0f);
    juce::ColourGradient surface(NimbusStyle::raised.brighter(0.08f), centreX, body.getY(),
                                 NimbusStyle::deep, centreX, body.getBottom(), false);
    g.setGradientFill(surface);
    g.fillEllipse(body);
    g.setColour(slider.isMouseOverOrDragging() && slider.isEnabled()
                    ? NimbusStyle::mutedGold : NimbusStyle::strongBorder.withAlpha(0.65f));
    g.drawEllipse(body.reduced(0.5f), 0.8f);

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

    if (id == "shimmerRatio")
    {
        const int positions = juce::roundToInt(slider.getMaximum() - slider.getMinimum()) + 1;
        g.setColour(NimbusStyle::secondary.withAlpha(slider.isEnabled() ? 0.55f : 0.25f));
        for (int i = 0; i < positions; ++i)
        {
            const auto detent = rotaryStartAngle + (rotaryEndAngle - rotaryStartAngle)
                                                   * (float) i / (float) juce::jmax(1, positions - 1);
            const auto inner = radius + 1.8f * scale;
            const auto outer = radius + 3.3f * scale;
            g.drawLine(centreX + std::sin(detent) * inner, centreY - std::cos(detent) * inner,
                       centreX + std::sin(detent) * outer, centreY - std::cos(detent) * outer,
                       0.8f * scale);
        }
    }

    // Thumb (indicator)
    juce::Path thumb;
    auto thumbWidth = (isMacro ? 2.0f : 1.5f) * scale;
    thumb.addRoundedRectangle(-thumbWidth * 0.5f, -bodyRadius * 0.88f, thumbWidth, bodyRadius * 0.44f, 0.7f);
    g.setColour(slider.isEnabled() ? NimbusStyle::text : NimbusStyle::secondary);
    g.fillPath(thumb, juce::AffineTransform::rotation(angle).translated(centreX, centreY));
}

void GreyCloudLookAndFeel::drawToggleButton(juce::Graphics& g, juce::ToggleButton& button,
                                            bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown)
{
    auto bounds = button.getLocalBounds().toFloat().reduced(1.0f);
    auto cornerSize = 4.0f;

    bool state = button.getToggleState();
    bool enabled = button.isEnabled();

    const auto hover = enabled && shouldDrawButtonAsHighlighted;
    g.setColour(state ? accentColour.withAlpha(shouldDrawButtonAsDown ? 0.28f : 0.16f)
                      : hover ? NimbusStyle::raised.brighter(0.08f) : NimbusStyle::deep.withAlpha(0.35f));
    g.fillRoundedRectangle(bounds, cornerSize);
    g.setColour(!enabled ? NimbusStyle::border : state ? accentColour.withAlpha(0.75f)
                                                               : hover ? NimbusStyle::strongBorder : NimbusStyle::border);
    g.drawRoundedRectangle(bounds, cornerSize, 1.0f);
    if (state && button.getName() == "hardFreeze")
    {
        g.setColour(accentColour);
        g.fillRoundedRectangle(bounds.withWidth(2.0f).reduced(0.0f, 4.0f), 1.0f);
    }
    g.setColour(!enabled ? NimbusStyle::secondary.withAlpha(0.55f) : state ? accentColour : NimbusStyle::text);
    g.setFont(NimbusStyle::controlFont(10.0f * button.getHeight() / (button.getName() == "sizeSync" ? 18.0f : button.getName() == "preDelaySync" ? 20.0f : 22.0f)));
    g.drawText(button.getButtonText(), button.getLocalBounds(), juce::Justification::centred, true);
}

void GreyCloudLookAndFeel::drawComboBox (juce::Graphics& g, int width, int height, bool isButtonDown,
                                         int buttonX, int buttonY, int buttonW, int buttonH, juce::ComboBox& box)
{
    auto cornerSize = 4.0f;
    juce::Rectangle<int> boxBounds(0, 0, width, height);
    
    g.setColour(box.isEnabled() ? backgroundColour : backgroundColour.darker());
    g.fillRoundedRectangle(boxBounds.toFloat(), cornerSize);
    
    g.setColour(box.isMouseOver() || isButtonDown ? NimbusStyle::strongBorder : NimbusStyle::border);
    g.drawRoundedRectangle(boxBounds.toFloat().reduced(0.5f, 0.5f), cornerSize, 1.0f);
    
    const auto scale = (float) height / (box.getName() == "syncDivision" ? 20.0f : box.getName() == "shimmerRatio" ? 24.0f : 22.0f);
    const auto arrowWidth = juce::roundToInt(20.0f * scale);
    juce::Rectangle<int> arrowZone(width - arrowWidth, 0, arrowWidth, height);
    juce::Path path;
    path.startNewSubPath(arrowZone.getX() + 5.0f * scale, arrowZone.getCentreY() - 2.0f * scale);
    path.lineTo(arrowZone.getCentreX(), arrowZone.getCentreY() + 3.0f * scale);
    path.lineTo(arrowZone.getRight() - 5.0f * scale, arrowZone.getCentreY() - 2.0f * scale);
    
    g.setColour(NimbusStyle::secondary);
    g.strokePath(path, juce::PathStrokeType(2.0f));
}

juce::Font GreyCloudLookAndFeel::getComboBoxFont(juce::ComboBox& box)
{
    return NimbusStyle::regularFont(11.5f * box.getHeight() / (box.getName() == "syncDivision" ? 20.0f : box.getName() == "shimmerRatio" ? 24.0f : 22.0f));
}

void GreyCloudLookAndFeel::positionComboBoxText(juce::ComboBox& box, juce::Label& label)
{
    const auto scale = (float) box.getHeight() / (box.getName() == "syncDivision" ? 20.0f : box.getName() == "shimmerRatio" ? 24.0f : 22.0f);
    label.setBounds(juce::Rectangle<int>(juce::roundToInt(6 * scale), 1, box.getWidth() - juce::roundToInt(26 * scale), box.getHeight() - 2));
    label.setFont(getComboBoxFont(box));
    label.setJustificationType(juce::Justification::centredLeft);
}

juce::Font GreyCloudLookAndFeel::getPopupMenuFont()
{
    return NimbusStyle::regularFont(11.5f);
}

juce::Font GreyCloudLookAndFeel::getSliderPopupFont(juce::Slider& slider)
{
    juce::ignoreUnused(slider);
    return NimbusStyle::regularFont(11.5f);
}

void GreyCloudLookAndFeel::drawButtonBackground(juce::Graphics& g, juce::Button& button,
                                                const juce::Colour& backgroundColourToUse,
                                                bool shouldDrawButtonAsHighlighted,
                                                bool shouldDrawButtonAsDown)
{
    juce::ignoreUnused(backgroundColourToUse);

    auto bounds = button.getLocalBounds().toFloat().reduced(1.0f);
    const auto cornerSize = 4.0f;

    g.setColour(shouldDrawButtonAsDown ? accentColour.withAlpha(0.22f)
                                      : shouldDrawButtonAsHighlighted ? accentColour.withAlpha(0.12f)
                                                                      : backgroundColour.darker(0.12f));
    g.fillRoundedRectangle(bounds, cornerSize);

    g.setColour(shouldDrawButtonAsHighlighted ? NimbusStyle::strongBorder : NimbusStyle::border);
    g.drawRoundedRectangle(bounds, cornerSize, 1.0f);
}

void GreyCloudLookAndFeel::drawButtonText(juce::Graphics& g, juce::TextButton& button,
                                          bool shouldDrawButtonAsHighlighted,
                                          bool shouldDrawButtonAsDown)
{
    juce::ignoreUnused(shouldDrawButtonAsHighlighted, shouldDrawButtonAsDown);

    g.setColour(button.isEnabled() ? NimbusStyle::text.withAlpha(0.88f) : NimbusStyle::secondary);
    g.setFont(NimbusStyle::regularFont(9.5f * button.getHeight() / 22.0f));
    g.drawFittedText(button.getButtonText(), button.getLocalBounds().reduced(5, 0),
                     juce::Justification::centred, 1);
}

void GreyCloudLookAndFeel::drawLinearSlider(juce::Graphics& g, int x, int y, int width, int height,
                                             float sliderPos, float minSliderPos, float maxSliderPos,
                                             const juce::Slider::SliderStyle style, juce::Slider& slider)
{
    auto trackH = 3.0f;
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

    auto thumbW = 5.0f;
    g.setColour(slider.isEnabled() ? NimbusStyle::text : NimbusStyle::secondary);
    g.fillRoundedRectangle(sliderPos - thumbW * 0.5f, y + height * 0.25f, thumbW, height * 0.5f, 1.5f);
}
